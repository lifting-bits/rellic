#
# Copyright (c) 2021-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

import ida_funcs
import ida_kernwin
import idautils
import anvill

import os
import json
import tempfile
import subprocess
from shutil import which


class rellic_decompile_function_t(ida_kernwin.action_handler_t):
    _anvill_decompile_json_path = None
    _rellic_decomp_path = None
    _opt_path = None

    def __init__(self):
        self.locate_external_programs()

        print("rellic: Found `opt` executable:", self._opt_path)
        print("rellic: Found `anvill-decompile-json` executable:",
              self._anvill_decompile_json_path)

        print("rellic: Found `rellic-decomp` executable:",
              self._rellic_decomp_path)

        print("rellic: Place the cursor inside a function and use the right-click menu, or press CTRL+Y")

    def locate_external_programs(self):
        opt_path = which("opt")
        if opt_path is None:
            return False

        llvm_version_list = ["12"]

        for llvm_version in llvm_version_list:
            anvill_decompile_json_path = which(
                "anvill-decompile-json-" + llvm_version)
            rellic_decomp_path = which("rellic-decomp-" + llvm_version + ".0")

            if anvill_decompile_json_path is not None and rellic_decomp_path is not None:
                remill_semantics_path = "/usr/local/share/remill/" + \
                    llvm_version + "/semantics"

                if not os.path.isdir(remill_semantics_path):
                    raise RuntimeError(
                        "rellic: anvill and rellic have been compiled for llvm {}, but the matching remill semantics were not found".format(llvm_version))

                self._opt_path = opt_path
                self._anvill_decompile_json_path = anvill_decompile_json_path
                self._rellic_decomp_path = rellic_decomp_path

                return True

        raise RuntimeError("rellic: Failed to locate rellic/anvill/remill/opt")

    def display_output(self, window_name, decompiled_function):
        # Make sure we have a newline at the end
        if not decompiled_function.endswith("\n"):
            decompiled_function.append("\n")

        # Create a window and display the output
        output_window = ida_kernwin.simplecustviewer_t()
        if output_window.Create(window_name):
            for line in decompiled_function.splitlines():
                output_window.AddLine(line)

            output_window.Show()

        else:
            print("rellic: Failed to create the display window")

    def activate(self, ctx):
        # Identify the function at the cursor
        screen_cursor = ida_kernwin.get_screen_ea()
        function_name = ida_funcs.get_func_name(screen_cursor)
        if function_name is None:
            print("rellic: The cursor is not located inside a function")
            return

        # Generate the anvill spec data
        anvill_program = anvill.get_program()

        try:
            anvill_program.add_function_definition(screen_cursor)

        except Exception as e:
            print("rellic: Failed to process the function at address {:x}. Error details follow:\n{}".format(
                screen_cursor, e))

            return

        spec_data = json.dumps(anvill_program.proto(),
                               sort_keys=False, indent=2)

        # Create a temporary directory and keep it around, since we may have to
        # debug issues in anvill/rellic
        working_directory = tempfile.mkdtemp()
        print("rellic: The working directory is located here: {}".format(
            working_directory))

        # Dump the anvill spec file to disk
        spec_file_path = os.path.join(working_directory, 'spec.json')
        with open(spec_file_path, "w+") as spec_file:
            spec_file.write(spec_data)
            spec_file.close()

        # Now ask anvill to generate the bc from the spec data
        bc_file_path = os.path.join(working_directory, "out.bc")
        ir_file_path = os.path.join(working_directory, "out.ir")

        try:
            subprocess.check_output([self._anvill_decompile_json_path, "-spec", spec_file_path,
                                     "-bc_out", bc_file_path, "-ir_out", ir_file_path], text=True, stderr=subprocess.STDOUT)

        except subprocess.CalledProcessError as e:
            print(
                "rellic: Failed to start the anvill-decompile-json process. Error details follow:\n{}".format(e.output))

            return

        # Run the reg2mem pass on the generated bitcode
        processed_bc_file_path = os.path.join(working_directory, "reg2mem.bc")

        try:
            subprocess.check_output([self._opt_path, "-reg2mem", bc_file_path,
                                     "-o=" + processed_bc_file_path], text=True, stderr=subprocess.STDOUT)

        except subprocess.CalledProcessError as e:
            print(
                "rellic: Failed to start the opt process. Error details follow:\n{}".format(e.output))

            return

        # Finally, ask rellic to decompile the bitcode
        c_file_path = os.path.join(working_directory, 'out.c')

        try:
            subprocess.check_output([self._rellic_decomp_path, "--input", processed_bc_file_path,
                                     "--output", c_file_path], text=True, stderr=subprocess.STDOUT)

        except subprocess.CalledProcessError as e:
            print(
                "rellic: Failed to start the rellic-decomp process. Error details follow:\n{}".format(e.output))

            return

        with open(c_file_path, "r") as rellic_output_file:
            decompiled_function = rellic_output_file.read()

        self.display_output(function_name, decompiled_function)

    def update(self, ctx):
        if ctx.widget_type == ida_kernwin.BWN_DISASM:
            return ida_kernwin.AST_ENABLE_FOR_WIDGET

        return ida_kernwin.AST_DISABLE_FOR_WIDGET


ACTION_NAME = "decompile-with-rellic"

ida_kernwin.register_action(
    ida_kernwin.action_desc_t(
        ACTION_NAME,
        "Decompile with rellic",
        rellic_decompile_function_t(),
        "Ctrl+Y"))


class popup_hooks_t(ida_kernwin.UI_Hooks):
    def finish_populating_widget_popup(self, w, popup):
        if ida_kernwin.get_widget_type(w) == ida_kernwin.BWN_DISASM:
            ida_kernwin.attach_action_to_popup(w, popup, ACTION_NAME, None)


hooks = popup_hooks_t()
hooks.hook()

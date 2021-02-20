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
    def get_program_suffix(self):
        suffix_list = ["9.0", "10.0", "11.0"]

        for suffix in suffix_list:
            if which("anvill-decompile-json-" + suffix) is None:
                continue

            if which("rellic-decomp-" + suffix) is None:
                continue

            return suffix

        return None

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
        program_suffix = self.get_program_suffix()
        print("rellic: Using tools for LLVM version {}".format(program_suffix))

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
            subprocess.check_call(["anvill-decompile-json-" + program_suffix, "-spec",
                                   spec_file_path, "-bc_out", bc_file_path, "-ir_out", ir_file_path])

        except Exception as e:
            print(
                "rellic: Failed to start the anvill-decompile-json process. Error details follow:\n{}".format(e))

            return

        # Finally, ask rellic to decompile the bitcode
        c_file_path = os.path.join(working_directory, 'out.c')

        try:
            subprocess.check_call(
                ["rellic-decomp-" + program_suffix, "--input", bc_file_path, "--output", c_file_path])

        except Exception as e:
            print(
                "rellic: Failed to start the rellic-decomp process. Error details follow:\n{}".format(e))

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

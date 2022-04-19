# `rellic-xref`

## What is it?

`rellix-xref` is an interactive, web-based interface for Rellic. Whereas `rellic-decomp` provides a batch approach to decompiling LLVM modules, `rellic-xref` aims to be a more exploratory tool. Its purpose is to bring more insight into the inner workings of Rellic's lifting process and refinement passes by allowing the user the choice of which passes to apply, and in what order.
It also provides visual feedback regarding the way the original bitcode has been lifted into C source code: hovering over parts of the C AST will highlight the bitcode that generated it, and vice versa.

## How do I use it?

After compiling Rellic, you can launch the `rellic-xref` executable supplying the following arguments:

* `--address`: Tells `rellic-xref` to listen for connections from a specific address. Defaults to `0.0.0.0`, which means all addresses are considered valid.
* `--port`: TCP port on which the HTTP server will listen. Defaults to `80`.
* `--home`: Path where `rellic-xref`'s assets are found. Should point to the `www` directory that is supplied alongside this README.
* `--angha`: Path to a directory containing AnghaBench test files. Supplying the files allows the server to load them directly without uploading through the interface. If not needed, point this to an empty directory.

As an example, at Trail of Bits we have an instance of `rellic-xref` running on a private VPS. To provide automatic restarts in the event of crashes, it is configured as a `systemd` service. The following is an example of what such a service file would look like:

```systemd
[Unit]
Description=rellic-xref daemon
After=network-online.target

[Service]
Type=simple
ExecStart=/path/to/rellic-xref --address=0.0.0.0 --port=80 --home=/path/to/www --logtostderr=1 --angha=/path/to/angha/bitcode
Restart=on-failure
StandardError=journal

[Install]
WantedBy=multi-user.target
```

## Should I expose this publicly to the internet?

NO! This tool is only meant for local / intranet use, **never** expose it to untrusted users.

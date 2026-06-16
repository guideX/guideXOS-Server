# gxbuild

`gxbuild` is the guideXOS Server developer command line tool for building a universal `.gxapp` application package.

It is CI-friendly: all options can be passed on the command line or stored in a JSON config file.

## Supported targets

- x86
- amd64
- arm
- arm64
- ia64
- loongarch64
- mips64
- ppc64
- sparc
- sparc64

## Basic usage

```bash
tools/gxbuild \
  --name MyApp \
  --version 1.0.0 \
  --required-guidexos 0.1.0 \
  --targets amd64,arm64,sparc64,ia64 \
  --source apps/MyApp \
  --build-root build/gxbuild/MyApp \
  --build-command "make -C {source} ARCH={arch} OUT={output}" \
  --binary-template "{build_dir}/app.bin" \
  --output MyApp.gxapp
```

On Windows:

```cmd
tools\gxbuild.cmd --name MyApp --version 1.0.0 --required-guidexos 0.1.0 --targets amd64 --skip-build --binary-template "prebuilt\{arch}\app.bin" --output MyApp.gxapp
```

## Config file

```json
{
  "name": "MyApp",
  "version": "1.0.0",
  "required_guidexos": "0.1.0",
  "source": "apps/MyApp",
  "output": "MyApp.gxapp",
  "build_root": "build/gxbuild/MyApp",
  "targets": ["amd64", "arm64", "sparc64", "ia64"],
  "build_command": "make -C {source} ARCH={arch} OUT={output}",
  "binary_template": "{build_dir}/app.bin",
  "entry_point": "_start"
}
```

Run:

```bash
tools/gxbuild --config gxbuild.json
```

## Command templates

The following placeholders are available in `build_command` and `binary_template`:

- `{arch}` - current target architecture
- `{source}` - source directory
- `{build_dir}` - per-architecture build directory
- `{output}` - expected per-architecture output binary

## Pipeline behavior

`gxbuild` exits with:

- `0` on success
- the build command exit code if compilation fails
- `1` for configuration, packaging, or missing-binary errors

The generated package uses the guideXOS native GXAPP container format:

```text
GXAPP header
metadata.json
bin/<arch>/app.bin
```

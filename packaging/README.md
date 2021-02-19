# rellic packaging scripts

## How to generate packages

1. Configure and build rellic
2. Set the **DESTDIR** variable to a new folder
3. Run the packaging script, passing the **DESTDIR** folder

Example:

```sh
rellic_version=$(git describe --always)

cpack -D RELLIC_DATA_PATH="/path/to/install/directory" \
      -R ${rellic_version} \
      --config "packaging/main.cmake"
```

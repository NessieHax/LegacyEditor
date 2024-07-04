# LegacyEditor Project Documentation

My goal is to write the code necessary to convert saves between
all "Minecraft Console Edition" consoles and all their specific versions. It will also be able to handle
player conversion, and be usable in such a way to be easily scriptable
for editing blocks / nbt.

## Supported Consoles and Formats

### Reading From:
- **WiiU**
- **PS3**
- **RPCS3 Emulator**
- **PSVita**
- **Xbox 360 (.bin format)**
- **Xbox 360 (.dat format)**
- **PS4** (entity conversion missing)
- **Switch** (region/entity conversion missing)

### Writing To:
- **WiiU**
- **PSVita**
- **PS3** (METADATA not yet resignable)
- **RPCS3 Emulator** (METADATA not yet resignable)

## Usage

Refer to the `examples/` directory to see different ways the code can be used. For unit testing, edit the folder locations in `LegacyEditor/examples/unit_tests.cpp` to the directory that contains your saves (e.g., `/saves/`).

## Dependencies

This project makes use of several external libraries, including:
- [gulrak/filesystem](https://github.com/gulrak/filesystem) for filesystem operations
- [stb](http://nothings.org/stb) by Sean Barrett
- [jibsen/tinf](https://github.com/jibsen/tinf) for data compression
- LZX - Jed Wing <jedwin@ugcs.caltech.edu>
- TINF - Joergen Ibsen
- SFO - [hippie68 @github](https://github.com/hippie68/sfo)

## Submodules

This project uses a [separate project](https://github.com/zugebot/lce.git).
Set this up by doing:
```bash
git submodule add https://github.com/zugebot/lce.git
git submodule update --init
```
To use this as a submodule in another project, you can do
```bash
git submodule add https://github.com/zugebot/LegacyEditor.git
git submodule update --init
```

## Outside Help

PS-VITA: https://docs.google.com/document/d/1HUoeH9YcIwqYPYMx9ps0Ui3YF0x_g9QkluADAx_fTJQ

## License

Please refer to `LICENSE.md` for detailed information on the licensing of this code and its usage permissions.

---
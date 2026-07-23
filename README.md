
# Dusklight Asset Tools
A tool to assist in dusklight mod creation.

The purpose of this tool is to take a `.iso` file containing assets for dusklight, and recursively unpack that into its smallest serializable form using modern standardized file formats. The unpacked files can then be re-packed into the nearest-available binary match.

## File Format Support
The program currently supports packing and unpacking the following formats:
|Format|Support  |
|--|--|
| .iso | Full |
| .arc | Full |
| .bti -> .png | Full |
| .ast -> .wav | Full |
| Text (.bmg) | Planned |
| J2D (.blo) | Planned |
| J3D (.bmd, .btk, ) | Planned |
| JAudio (.bms, .aw, .baa) | Planned |
| Stage (.dzs, .dzr) | Planned |
| Collision (.kcl, .plc) | Planned |
| Event (.dat, .stb) | Planned |

## Usage
### Full Iso
```bash
# Unpack an iso to a directory
./dusklight-assets unpack GZ2E01.iso GZ2E01/
# Pack the unpacked iso into an intermediate directory (assets will be re-packed only when edited)
./dusklight-assets pack GZ2E01/ intermediate.iso/
#Pack the intermediate iso into a full iso
./dusklight-assets pack intermediate.iso/ GZ2E01_packed.iso
```
### Dusklight Mod
```bash
# Unpack an iso to a directory
./dusklight-assets unpack GZ2E01.iso GZ2E01/
# Create an overlay source folder for the asset you wish to include in your mod
mkdir -p overlay_src/res/Layout/main2D.c.arc
# Copy your unpacked directory to the overlay source folder
cp -r GZ2E01/files/res/Layout/main2D.c.arc overlay_src/res/Layout/main2D.c.arc
# Pack the files in overlay_src to your bundled overlay directory
./dusklight-assets pack overlay_src overlay/
# Build your mod and the re-packed assets will be included
```

## Building
1. Clone the repository
```bash
git clone https://github.com/jdflyer/dusklight-asset-tools
```
2. Build locally
```bash
cmake -B build
cmake --build build
```
3. The compiled binary will be at `build/dusklight-assets`

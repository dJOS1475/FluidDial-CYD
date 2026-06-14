Import("env")
import shutil, os

flash_size = env.BoardConfig().get("upload.flash_size", "detect")

merge_cmd = '$PYTHONEXE $UPLOADER --chip $BOARD_MCU merge_bin --output $BUILD_DIR/merged-flash.bin --flash_mode dio --flash_size ' + flash_size + " "

for image in env.get("FLASH_EXTRA_IMAGES", []):
    merge_cmd += image[0] + " " + env.subst(image[1]) + " "

filesystem_start = env.GetProjectOption("custom_filesystem_start", "Missing_custom_filesystem_start_variable")
merge_cmd += " 0x10000 $BUILD_DIR/firmware.bin " + filesystem_start + " $BUILD_DIR/littlefs.bin"


def copy_to_docs(target, source, env):
    """Copy every flashable artefact into docs/ for the web installer.

    The merged image is kept for the 'Factory Reset' path (single blob written
    at offset 0, wipes NVS).  The individual partition images are needed by the
    NVS-preserving 'Update' path — esp-web-tools writes each at its own offset
    and leaves the gaps (including the NVS partition at 0x9000) untouched.
    """
    build_dir   = env.subst("$BUILD_DIR")
    docs_dir    = os.path.join(env.subst("$PROJECT_DIR"), "docs")
    # boot_app0.bin lives in the Arduino-ESP32 framework, not the build dir.
    framework   = env.PioPlatform().get_package_dir("framework-arduinoespressif32") or ""
    boot_app0   = os.path.join(framework, "tools", "partitions", "boot_app0.bin")

    artefacts = [
        # (source absolute path, basename for docs/)
        (os.path.join(build_dir, "merged-flash.bin"), "merged-flash.bin"),
        (os.path.join(build_dir, "bootloader.bin"),   "bootloader.bin"),
        (os.path.join(build_dir, "partitions.bin"),   "partitions.bin"),
        (os.path.join(build_dir, "firmware.bin"),     "firmware.bin"),
        (os.path.join(build_dir, "littlefs.bin"),     "littlefs.bin"),
        (boot_app0,                                   "boot_app0.bin"),
    ]
    for src, name in artefacts:
        dst = os.path.join(docs_dir, name)
        if os.path.isfile(src):
            shutil.copy2(src, dst)
            print(f"Copied {name} -> docs/")
        else:
            print(f"WARNING: {name} not found at {src}")


env.AddCustomTarget(
    name="build_merged",
    dependencies=["$BUILD_DIR/bootloader.bin", "$BUILD_DIR/firmware.bin"],
    actions=[
        "pio run -e $PIOENV -t buildfs",
        merge_cmd,
        copy_to_docs,
    ],
    title="Build Merged",
    description="Build combined image, copy all install artefacts (merged + individual) to docs/"
)

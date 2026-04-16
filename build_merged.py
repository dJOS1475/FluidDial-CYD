Import("env")
import shutil, os

flash_size = env.BoardConfig().get("upload.flash_size", "detect")

merge_cmd = '$PYTHONEXE $UPLOADER --chip $BOARD_MCU merge_bin --output $BUILD_DIR/merged-flash.bin --flash_mode dio --flash_size ' + flash_size + " "

for image in env.get("FLASH_EXTRA_IMAGES", []):
    merge_cmd += image[0] + " " + env.subst(image[1]) + " "

filesystem_start = env.GetProjectOption("custom_filesystem_start", "Missing_custom_filesystem_start_variable")
merge_cmd += " 0x10000 $BUILD_DIR/firmware.bin " + filesystem_start + " $BUILD_DIR/littlefs.bin"

def copy_to_docs(target, source, env):
    build_dir = env.subst("$BUILD_DIR")
    src = os.path.join(build_dir, "merged-flash.bin")
    dst = os.path.join(env.subst("$PROJECT_DIR"), "docs", "merged-flash.bin")
    if os.path.isfile(src):
        shutil.copy2(src, dst)
        print(f"Copied merged-flash.bin → docs/merged-flash.bin")
    else:
        print("WARNING: merged-flash.bin not found — docs/ not updated")

env.AddCustomTarget(
    name="build_merged",
    dependencies=["$BUILD_DIR/bootloader.bin", "$BUILD_DIR/firmware.bin"],
    actions=[
        "pio run -e $PIOENV -t buildfs",
        merge_cmd,
        copy_to_docs,
    ],
    title="Build Merged",
    description="Build combined image with program and filesystem, copy to docs/"
)

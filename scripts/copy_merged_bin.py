Import("env")
import shutil, os

def copy_merged_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    src = os.path.join(build_dir, "merged-flash.bin")
    dst = os.path.join(env.subst("$PROJECT_DIR"), "docs", "merged-flash.bin")
    if os.path.isfile(src):
        shutil.copy2(src, dst)
        print(f"Copied merged-flash.bin → docs/merged-flash.bin")
    else:
        print("merged-flash.bin not found in build dir — skipping copy")

env.AddPostAction("$BUILD_DIR/merged-flash.bin", copy_merged_bin)

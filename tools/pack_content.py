"""Builds the folder packed into the .wuhb from build/content : the same files, with the big map assets
zstd-compressed under their own names (the runtime sees the zstd frame magic and decompresses, see
gxFileSystem::readWholeFile). The console reads its SD card at about 12 MB/s, so loading a new game was mostly
spent reading : textures (.wbc / .wbc3), room meshes (.rmesh) and models (.b3d) shrink to 20-45 %.

Other files are hard links (no copy). Compressed files are only redone when their source changed.

Usage : py -I tools/pack_content.py <content dir> <packed dir> <zstd.exe>
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

COMPRESS = {'.wbc', '.wbc3', '.rmesh', '.b3d'}
LEVEL = '-19'


def same_file(a, b):
    try:
        sa, sb = os.stat(a), os.stat(b)
    except OSError:
        return False
    return sa.st_ino == sb.st_ino and sa.st_dev == sb.st_dev and sa.st_ino != 0


def main():
    src_root, dst_root, zstd = sys.argv[1], sys.argv[2], sys.argv[3]
    wanted = set()
    jobs = []
    linked = 0
    for dirpath, dirnames, filenames in os.walk(src_root):
        rel_dir = os.path.relpath(dirpath, src_root)
        dst_dir = os.path.normpath(os.path.join(dst_root, rel_dir))
        os.makedirs(dst_dir, exist_ok=True)
        wanted.add(os.path.normcase(dst_dir))
        for name in filenames:
            src = os.path.join(dirpath, name)
            dst = os.path.join(dst_dir, name)
            wanted.add(os.path.normcase(dst))
            if os.path.splitext(name)[1].lower() in COMPRESS:
                if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src) or same_file(src, dst):
                    jobs.append((src, dst))
            elif not same_file(src, dst):
                if os.path.lexists(dst):
                    os.remove(dst)
                try:
                    os.link(src, dst)
                except OSError:
                    import shutil
                    shutil.copy2(src, dst)
                linked += 1

    def compress(job):
        src, dst = job
        tmp = dst + '.tmp'
        subprocess.run([zstd, '-q', '-f', LEVEL, src, '-o', tmp], check=True)
        if os.path.lexists(dst):
            os.remove(dst)
        os.replace(tmp, dst)

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        list(pool.map(compress, jobs))

    # files that are no longer in the content folder
    removed = 0
    for dirpath, dirnames, filenames in os.walk(dst_root, topdown=False):
        for name in filenames:
            p = os.path.join(dirpath, name)
            if os.path.normcase(p) not in wanted:
                os.remove(p)
                removed += 1
        if os.path.normcase(os.path.normpath(dirpath)) not in wanted and dirpath != dst_root:
            try:
                os.rmdir(dirpath)
            except OSError:
                pass
    print('pack_content: %d compressed, %d linked, %d removed' % (len(jobs), linked, removed))


if __name__ == '__main__':
    main()

import os
import shutil
import filecmp

def sync_tree_left_to_right(src, dst):
    if not os.path.exists(dst):
        os.makedirs(dst)
    comp=filecmp.dircmp(src,dst)

    # copy left items that are not in right (ignoring *.gz.h files)
    for item in comp.left_only:
        # if item is a file
        if os.path.isfile(os.path.join(src,item)):
            if not item.endswith('.gz.h'):
                print('Copying %s' % os.path.join(src,item))
                shutil.copy2(os.path.join(src,item),os.path.join(dst,item))
        # else item is a dir
        else:
            print('Copying %s' % os.path.join(src,item))
            shutil.copytree(os.path.join(src,item),os.path.join(dst,item),ignore=shutil.ignore_patterns('*.gz.h'))

    # copy files which are different (ignoring *.gz.h files)
    for item in comp.diff_files:
        if not item.endswith('.gz.h'):
            print('Copying %s' % os.path.join(src,item))
            shutil.copy2(os.path.join(src,item),os.path.join(dst,item))

    # remove items that are not in left (ignoring *.gz.h files)
    for item in comp.right_only:
        itemPath=os.path.join(dst,item)
        if os.path.isfile(itemPath):
            if not item.endswith('.gz.h'):
                print('Removing %s' % itemPath)
                os.remove(itemPath)
        else:
            print('Removing %s' % itemPath)
            shutil.rmtree(itemPath)

    # remove *.gz.h files if * file does not exist
    for item in comp.right_only:
        if item.endswith('.gz.h') and os.path.isfile(os.path.join(dst,item)) and not os.path.isfile(os.path.join(dst,item[:-5])):
            print('Removing %s' % os.path.join(dst,item))
            os.remove(os.path.join(dst,item))

    # recursively sync common items
    for item in comp.common_dirs:
        sync_tree_left_to_right(os.path.join(src,item),os.path.join(dst,item))

print('---- pio_pre_copy_base.py start ----')

sourceFolder = r'../WBase/src/base'
destinationFolder = r'./src/base'

if not os.path.exists(r'./src/WBase.h') and os.path.exists(sourceFolder) and os.path.abspath(sourceFolder).lower() != os.path.abspath(destinationFolder).lower():
    sync_tree_left_to_right(sourceFolder, destinationFolder)

print('---- pio_pre_copy_base.py end ----')
print()
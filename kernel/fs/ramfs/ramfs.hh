#include "types.hh"

#include "printer.hh"

#include "fs/vfs/fs.hh"
#include "fs/ramfs/ramfsSb.hh"


#include <EASTL/string.h>

namespace fs{
    
    class dentry;


    namespace ramfs{

        class RamFSDen;

        class RamFS : public FileSystem
        {
            uint _device;
            eastl::string _fstype;
            ramfs::RamFSSb *_super_block;    // super block            
            bool _isroot;
            dentry *_root;
            dentry *_mnt;
            uint32 _ino = 0;

            public:
                void initfd(); // init ramfs, especially for some essential directories

                RamFS();
                RamFS( const RamFS &fs ) = default;
                RamFS( bool isroot_ ) : _isroot( isroot_ ) {};
                
                ~RamFS() = default;
		        void init( int dev, uint64 start_lba, eastl::string fstype, eastl::string rootname, bool is_root ) override { printfYellow(" ramfs don't support this function "); } // 
                bool isRootFS() const override { return _isroot; }
                SuperBlock *getSuperBlock() const override { return _super_block; }
                size_t rBlockSize() const override { return 512; } // RamFS does not have block size
                long rBlockNum() const override { return 0; } // RamFS does not have block number
                long rBlockFree() const override { return 0; } // RamFS does not have free block number
                long rMaxFile() const override { return 0; } // RamFS does not have max file number
                long rFreeFile() const override { return 0x100; } // RamFS does not have free file number
                eastl::string rFStype() const override { return _fstype; }
                size_t rNamelen() const override { return 512; } // RamFS does not have max name length
                dentry *getRoot() const override ;// RamFS does not have root dentry
                dentry *getMntPoint() const override { return _mnt; } // RamFS does not have mount point dentry
                FileAttrs rDefaultMod() const override { return _super_block->rDefaultMod(); }
                int alloc_ino() { return _ino++; }
                int mount( dentry *dev, dentry *mnt, eastl::string fstype ) override ;
                int umount( dentry *mnt ) override ;
                long rMagic() const override { return 0x9fa0; } // RamFS does not have magic number
        };

        extern RamFS k_ramfs;
    } // ramfs
}
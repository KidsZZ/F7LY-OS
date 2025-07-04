//
// Copy from Li Shuang ( pseudonym ) on 2024-07-22 
// --------------------------------------------------------------
// | Note: This code file just for study, not for commercial use 
// | Contact Author: lishuang.mk@whu.edu.cn 
// --------------------------------------------------------------
//

#pragma once

#include "fs/vfs/fs.hh"
#include "fs/ext4/ext4_sb.hh"
#include "fs/ext4/ext4_inode.hh"
#include "fs/ext4/ext4_block_group.hh"
#include "fs/ext4/ext4_buffer.hh"

#include "spinlock.hh"
#include "function.hh"

namespace fs
{
	int ext4_do_rename(const char* old_path, const char* new_path);
	namespace ext4
	{
		class Ext4FS : public FileSystem
		{
			friend Ext4BlockGroup;
		private:
			SpinLock _lock;
			bool _is_root = false;

			int _owned_dev = -1;
			ulong _start_lba = 0;
			Ext4SB _sb;
			Ext4IndexNode *_root_inode = nullptr;

			dentry * _root_dir = nullptr;

			int _cache_group_count = 0;
			int _cache_blocks_per_group = 0;
			int _cache_inodes_per_group = 0;
			Ext4BlockGroup * _bgs = nullptr;				// block GroupS

			Ext4BufferPool _blocks_cacher;

			long _s_indirect_block_start = 0;				// single indirect start block
			long _d_indirect_block_start = 0;				// double indirect start block
			long _t_indirect_block_start = 0;				// triple indirect start block

		public:
			Ext4FS() = default;
			virtual ~Ext4FS() override;
			virtual size_t rBlockSize() const override { return _sb.rBlockSize(); };
			virtual long rBlockNum() const override { return _sb.rBlockNum(); };
			virtual long rBlockFree() const override { return _sb.rBlockFree(); };
			virtual long rMaxFile() const override { return 10000; };
			virtual long rFreeFile() const override { return 10000; };
			virtual size_t rNamelen() const override { return 256; };
			virtual long rMagic() const override { return 0xef53; };
			virtual dentry *getRoot() const override { return _root_dir; };
			virtual dentry *getMntPoint() const override { return _sb.getMntPoint(); };
			virtual FileAttrs rDefaultMod() const override { return _sb.rDefaultMod(); };
			virtual int mount( dentry *dev, dentry *mnt, eastl::string fstype ) override { return -1; };
			virtual int umount( dentry *mnt ) override { return -1; };
			virtual bool isRootFS() const override { return _is_root; }
			virtual SuperBlock *getSuperBlock() const override { return ( SuperBlock * ) &_sb; };
			virtual eastl::string rFStype() const override { return "ext4"; }

		public:
			virtual void init( int dev, u64 start_lba, eastl::string fstype, eastl::string rootname, bool is_root = false ) override;

		public:
			Ext4SuperBlock * get_ext4_superblock() const { return ( Ext4SuperBlock * ) &_sb._super_block; }
			bool support64bit() const { return _sb.support64bit(); }
			int owned_device() const { return _owned_dev; }
			ulong start_lba() const { return _start_lba; }
			long read_inode_size() const { return _sb._super_block.inode_size; }
			long get_sind_block_start() const { return _s_indirect_block_start; }
			long get_dind_block_start() const { return _d_indirect_block_start; }
			long get_tind_block_start() const { return _t_indirect_block_start; }
			void get_hash_seed( u32 hash[ 4 ] ) const
			{
				hash[ 0 ] = _sb._super_block.hash_seed[ 0 ];
				hash[ 1 ] = _sb._super_block.hash_seed[ 1 ];
				hash[ 2 ] = _sb._super_block.hash_seed[ 2 ];
				hash[ 3 ] = _sb._super_block.hash_seed[ 3 ];
			}

			Ext4Buffer * read_block( long block_no, bool pin = false ) { return _blocks_cacher.request_block( block_no, pin ); }

			/// @brief 计算 inode 归属的块组
			/// @param inode_no inode 号
			/// @return 块组号
			long inode_bg_no( long inode_no ) { return ( inode_no - 1 ) / _cache_inodes_per_group; }

		public:
			/// @brief 从指定块号开始读取数据
			/// @param block_no 块号
			/// @param dst 保存数据的地址
			/// @param size 读取数据的长度（字节）
			void read_data( long block_no, void * dst, long size );

			int read_inode( long inode_no, Ext4Inode &node );

			/// @brief 分配一个inode号
			/// @param fa 
			/// @param inr 
			/// @return 
			int allocate_inode( fs::FileAttrs fa, long &inr );

		public:
			Ext4IndexNode * debug_get_root_inode() { return _root_inode; }
		};

	} // namespace ext4

} // namespace fs

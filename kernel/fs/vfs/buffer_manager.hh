

#pragma once 

#include "buffer.hh"
// #include "klib/common.hh"

#include "printer.hh"
#include "spinlock.hh"

namespace fs
{

	constexpr uint block_per_pool_shift = 6;
	constexpr uint block_per_pool = 1U << block_per_pool_shift;


	/*****************************************************************
	 The 48-bit LBA dividing shall be like this:
	  ------------------------------------------------------
	 |  bit  | 47       9 | 8          3 | 2              0 |
	 | ----- | ---------- | ------------ | ---------------- |
	 | usage | tag number | block number | offset in buffer |
	  ------------------------------------------------------
	 *****************************************************************/

#define _build_lba_divide_( name, shift, mask_width ) \
	constexpr uint64 lba_##name##_shift = shift; \
	constexpr uint64 lba_##name##_mask = (~0UL) >> (64-mask_width) << lba_##name##_shift

	_build_lba_divide_( offset, 0, sector_per_buffer_shift );
	_build_lba_divide_( blk_num, lba_offset_shift + sector_per_buffer_shift, block_per_pool_shift );
	_build_lba_divide_( tag_num, lba_blk_num_shift + block_per_pool_shift, 48 - lba_blk_num_shift - lba_offset_shift );
#undef _build_lba_divide_

	class BufferManager
	{
	private:
		SpinLock _lock;
		BufferBlock _buffer_pool[ block_per_pool ];

	public:
		BufferManager() {};
		void init( const char *lock_name );

		Buffer read( int dev, uint64 lba );

		Buffer read_sync( int dev, uint64 lba );

		void write( int dev, uint64 lba );

		void write_sync( int dev, uint64 lba );

		/// @brief 将一块Buffer标记为有脏数据，在合适的时候会进行回写
		/// @return 标记失败返回负值
		int dirt_buffer( Buffer &buf );

		/// @brief 刷新一块buffer到磁盘
		/// @return 刷新失败返回负数
		int flush_buffer( Buffer &buf );

		/// @brief 同步方法，比较费时，应当在调度开始前供内核使用
		void release_buffer_sync( Buffer &buf )
		{
			_release_buffer( buf, false );
		}
		/// @brief 异步方法，允许进程间调度
		void release_buffer( Buffer &buf )
		{
			_release_buffer( buf, true );
		}

		/// @brief 将该缓存pin住，使其长时间保留在内存中
		void pin_buffer( Buffer &buf )
		{
			_lock.acquire();
			_buffer_pool[ buf._block_number ]._ref_cnt[ buf._buffer_index ]++;
			_lock.release();
		}

		/// @brief 不保证缓存会被刷新到磁盘，只解除一次pin
		void unpin_buffer( Buffer &buf )
		{
			_lock.acquire();
			_buffer_pool[ buf._block_number ]._ref_cnt[ buf._buffer_index ]--;
			_lock.release();
		}

	private:

		/// @brief 将device映射为SATA端口号
		/// @details todo: 
		///          此处的函数只是简单的恒同映射
		///          在后续完善设备管理后可能发生变化
		/// @param dev 虚拟设备号
		/// @return SATA端口号，超过32是不合法的
		int _dev_to_sata_port( int dev );

		/// @brief 尝试访问buffer，buffer不存在会分配一个可用的buffer。这个方法通常不应该直接调用，应当使用async/sync后缀的方法。
		/// @param need_sleep_lock 是否需要 sleep-lock，通常而言，这个用于异步场景
		/// @return 
		Buffer _get_buffer( int dev, uint64 lba, bool need_sleep_lock );

		/// @brief 每次 get buffer 并使用完后，应当将其释放
		/// @param used_sleep_lock 是否使用了 sleep-lock，这与 _get_buffer() 方法是一致的
		void _release_buffer( Buffer &buf, bool used_sleep_lock );

		/// @brief 检查该设备是否是块设备
		int _check_block_device( uint dev_num );


		/// @brief 同步方法，比较费时，应当在调度开始前供内核使用
		Buffer _get_buffer_sync( int dev, uint64 lba )
		{
			return _get_buffer( dev, lba, false );
		}
		/// @brief 异步方法，允许进程间调度
		Buffer _get_buffer_async( int dev, uint64 lba )
		{
			return _get_buffer( dev, lba, true );
		}

		// 对buffer块号和索引号进行断言
		void _assert_blk_or_idx( uint blk, uint idx )
		{
			assert( blk < block_per_pool,
				"块号不合法: %d ( 0 ~ %d )",
				blk, block_per_pool - 1
			);
			assert( idx < _buffer_pool[ blk ]._current_buffer_counts,
				"buffer索引不合法: %d ( 0 ~ %d )",
				idx, _buffer_pool[ blk ]._current_buffer_counts - 1
			);
		}

		// 测试对应buffer是否有效
		bool _buf_is_valid( uint blk, uint idx );
		// 测试对应buffer是否有脏数据
		bool _buf_is_dirty( uint blk, uint idx );
		// 测试对应buffer是否被硬盘持有
		bool _buf_is_disk_own( uint blk, uint idx );
		// 对应buffer设置为有效
		void _buf_set_valid( uint blk, uint idx );
		// 对应buffer设置为无效
		void _buf_reset_valid( uint blk, uint idx );
		// 对应buffer设置脏位
		void _buf_set_dirty( uint blk, uint idx );
		// 对应buffer复位脏位
		void _buf_reset_dirty( uint blk, uint idx );
		// 对应buffer设置硬盘持有位
		void _buf_set_disk_own( uint blk, uint idx );
		// 对应buffer复位硬盘持有位
		void _buf_reset_disk_own( uint blk, uint idx );

		uint _offset_from_lba( uint64 lba ) { return ( uint ) ( lba & lba_offset_mask ) >> lba_offset_shift; }
		uint64 _blk_num_from_lba( uint64 lba ) { return ( lba & lba_blk_num_mask ) >> lba_blk_num_shift; }
		uint64 _tag_num_from_lba( uint64 lba ) { return ( lba & lba_tag_num_mask ) >> lba_tag_num_shift; }
		uint64 _lba_blk_align( uint64 lba ) { return lba & ~lba_offset_mask; }
		uint64 _lba_from_tag_blk_off( u64 tag, u64 block_num, u64 offset )
		{
			return ( ( tag << lba_tag_num_shift ) & lba_tag_num_mask )
				| ( ( block_num << lba_blk_num_shift ) & lba_blk_num_mask )
				| ( ( offset << lba_offset_shift ) & lba_offset_mask );
		}
	};

	extern BufferManager k_bufm;
} // namespace fs

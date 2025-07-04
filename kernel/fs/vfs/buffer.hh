

#pragma once 

#include "types.hh"
#include "sleeplock.hh"
// #include "klib/common.hh"
#include "platform.hh"
// #include <mem/page.hh>

namespace fs
{
	constexpr uint default_buffer_size = PGSIZE/*bytes*/;
	constexpr uint max_buffer_per_block = 64;

	// it is simplified here, which should be read from SATA device.
	constexpr uint default_sector_size = 512;
	constexpr uint sector_per_buffer = default_buffer_size / default_sector_size;
	constexpr uint calc_sector_per_buffer_shift(uint val) {
		uint _k = 0;
		while ((1U << _k) != val) ++_k;
		return _k;
	}
	constexpr uint sector_per_buffer_shift = calc_sector_per_buffer_shift(sector_per_buffer);
	constexpr uint sector_per_buffer_mask = sector_per_buffer - 1;
	static_assert( ( sector_per_buffer & sector_per_buffer_mask ) == 0, "sector-per-buffer should be a power of 2." );

	class BufferBlock;
	class BufferManager;
	class BufferNode;

	class Buffer
	{
		friend BufferManager;
		friend BufferBlock;
		friend BufferNode;
	private:
		void *_buffer_base;
		uint _block_number;
		uint _buffer_index;
	public:
		Buffer();
		Buffer( uint64 buffer_base, uint number, uint index );
		Buffer( void *buffer_base, uint number, uint index );

		const void * get_data_ptr()
		{
			return ( const void * ) _buffer_base;
		}

		/// @brief 复制buffer数据到指定地址
		void copy_data_to( void * dst )
		{
			long * _from = ( long* ) _buffer_base;
			long * _to = ( long* ) dst;
			const long count = default_sector_size / sizeof( long );
			for ( long i = 0; i < count; ++i )
				_to[ i ] = _from[ i ];
		}

		const void * get_end_ptr()
		{
			return ( const void * ) ( ( uint64 ) _buffer_base + default_buffer_size );
		}

	// for debugging
	public:
		uint64 debug_get_buffer_base() { return ( uint64 ) _buffer_base; }
	};

	/// @brief buffer 链表节点
	class BufferNode
	{
		friend BufferBlock;
		friend BufferManager;
	private:
		int _buf_index;
		BufferNode *_next;
		BufferNode *_prev;
	public:
		BufferNode();
	};

	/// @brief 缓冲区容器, 组织和管理一组相关的磁盘缓冲区
	/// @details 每个block拥有一个页面的空间, 每个buffer都是512byte，4K页面可以有8个buffer
	class BufferBlock
	{
		friend BufferManager;
	private:
		uint64 _valid_map;										// 对应位为1表示该buffer是最新的数据，可直接读写
		uint64 _dirty_map;										// 对应位为1表示该buffer的数据是脏数据，应当写回硬盘
		uint64 _disk_own_map;									// 对应位为1表示硬盘DMA正在使用这个buffer，内核不应该在此时修改对应的buffer
		int _device[ max_buffer_per_block ];					// buffer对应的虚拟设备号
		int _ref_cnt[ max_buffer_per_block ];					// buffer引用计数
		uint64 _tag_number[ max_buffer_per_block ];				// 块号归属当前块的唯一标识，实际上是LBA的高位
		proc::SleepLock _sleep_lock[ max_buffer_per_block ];		// 每个buffer均有一个睡眠锁，实际上是Manager使用和管理的

		BufferNode _nodes[ max_buffer_per_block ];				// buffer 链表节点
		BufferNode _node_head;									// buffer 链表头

		SpinLock _lock;
		uint _block_number;										// 当前block的块号
		uint _current_buffer_counts;							// 当前block拥有的buffer数量，是实际上的该块拥有的buffer数，可以动态更新
		void *_buffer_base[ max_buffer_per_block ];				// buffer的基地址
	public:
		BufferBlock() {};
		BufferBlock( const BufferBlock &b ) = delete;
		void init( uint block_number );

		BufferNode* search_buffer( int dev, uint block_num, uint64 tag_num );

		Buffer get_buffer( BufferNode* buf_node );

		BufferNode* alloc_buffer( int dev, uint block_num, uint64 tag_num );
	};
} // namespace fs

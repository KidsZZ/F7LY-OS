#include "fs/ramfs/ramfsInode.hh"
#include "fs/ramfs/ramfs.hh"
#include "fs/vfs/path.hh"
#include "types.hh"

#include "proc_manager.hh"
#include "tm/timer_manager.hh"

#include "klib.hh"

#include "device_manager.hh"
#include "stream_device.hh"

#include <mntent.h>

#include <EASTL/chrono.h>
namespace fs
{

	namespace ramfs
	{

		SuperBlock *RamInode::getSb() const
		{
			return belong_fs->getSuperBlock();
		}

		FileSystem *RamInode::getFS() const
		{
			return belong_fs;
		}

		Inode *RamInode::mknode(eastl::string name, FileAttrs attrs, eastl::string dev_name)
		{
			return belong_fs->getSuperBlock()->allocInode(attrs, dev_name);
		}

		dev_t RamInode::rDev() const
		{
			return -1; // 这里不应该经过， 应该交给Device的rDev()处理
		}

		static char busybox_conf[] =
			"# /etc/busybox.conf: SliTaz GNU/linux Busybox configuration.\n"
			"[SUID]\n"
			"# Allow command to be run by anyone.\n"
			"su = ssx root.root\n"
			// "passwd = ssx root.root"
			"loadkmap = ssx root.root\n"
			"mount = ssx root.root\n"
			"reboot = ssx root.root\n"
			"halt = ssx root.root\n"
			"poweroff = ssx root.root\n";

		size_t RamInode::nodeRead(uint64 dst_, size_t off_, size_t len_)
		{
						printfMagenta("it is a ram node\n");
			if (readable)
			{
				size_t read_len = (off_ + len_ > sizeof busybox_conf) ? ((sizeof busybox_conf) - off_) : len_;
				memcpy((void *)dst_, (void *)&busybox_conf[off_], read_len);
				return read_len;
			}
			else
				return 0;
		}

		int Exe::readlinkat(char *buf, size_t len)
		{
			proc::Pcb *cur_proc = proc::k_pm.get_cur_pcb();
			int copylen = 0;
			if (len > cur_proc->exe.length())
				copylen = cur_proc->exe.length();
			else
				copylen = len;

			memcpy(buf, cur_proc->exe.c_str(), copylen);

			return copylen;
		}

		size_t Mount::nodeRead(uint64 dst_, size_t off_, size_t len_)
		{
			int off = off_;
			size_t readbts = 0;
			int cnt = 0;
			for (auto it : fs::mnt_table)
			{
				// 设备名称 总大小 用量 可用量 已用量百分比 挂载点
				FileSystem *fs = it.second;
				fs::Path path(fs->getRoot());
				eastl::string row = fs->getRoot()->rName() + " \t" +
									eastl::string(it.first) + " \t" +
									eastl::string(it.second->rFStype()) + " \t" +
									"rw\n"; // 确保每个字段之间用空格或制表符分隔，并且行末尾有一个换行符
				// printf( "row: %s\n", row.c_str() );
				int readlen = row.length(); // 使用 row.length() 而不是 sizeof(row)
				int bytes;

				if (readlen + readbts < len_)
					bytes = readlen;
				else
					bytes = len_ - readbts;

				if (off < readlen)
				{
					memcpy((void *)dst_, row.c_str() + off, bytes);
					dst_ += bytes;
					readbts += bytes;
				}
				else
					bytes > off ? off = 0 : off -= bytes;

				cnt++;
			}
			return readbts;
		}

		size_t SymbleLink::nodeRead(uint64 dst_, size_t off_, size_t len_)
		{
			fs::Path path(target_path); /// @todo 检查目标文件存在与否，不存在就删除此符号链接
			return path.pathSearch()->getNode()->nodeRead(dst_, off_, len_);
		}

		size_t MemInfo::nodeRead(uint64 dst_, size_t off_, size_t len_)
		{
			int off = off_;
			size_t readbts = 0;
			int cnt = 0;
			for (auto it : fs::mnt_table)
			{
				// 设备名称 总大小 用量 可用量 已用量百分比 挂载点
				FileSystem *fs = it.second;
				fs::Path path(fs->getRoot());
				eastl::string row = fs->getRoot()->rName() + " \t" +
									eastl::string(it.first) + " \t" +
									eastl::string(it.second->rFStype()) + " \t" +
									"rw\n"; // 确保每个字段之间用空格或制表符分隔，并且行末尾有一个换行符
				// printf( "row: %s\n", row.c_str() );
				int readlen = row.length(); // 使用 row.length() 而不是 sizeof(row)
				int bytes;

				if (readlen + readbts < len_)
					bytes = readlen;
				else
					bytes = len_ - readbts;

				if (off < readlen)
				{
					memcpy((void *)dst_, row.c_str() + off, bytes);
					dst_ += bytes;
					readbts += bytes;
				}
				else
					bytes > off ? off = 0 : off -= bytes;

				cnt++;
			}
			return readbts;
		}

		size_t RTC::nodeRead(uint64 dst_, size_t off_, size_t len_)
		{
			[[maybe_unused]] tmm::tm tm_;
			int rdbytes = 0;

			uint32 off = off_;

			auto cur = eastl::chrono::system_clock::now();
			auto time = cur.time_since_epoch().count();
			auto bytes = sizeof( time );
			if( off < bytes )
			{
				memcpy( (void *)dst_, &time, bytes );
				dst_ += rdbytes;
				rdbytes += bytes;
			}

			if( off > 0 ) off -= bytes;
			return rdbytes;
		}

		size_t Device::nodeRead(uint64 dst_, size_t off_, size_t len_)
		{
			int ret;

			dev::StreamDevice *sdev = (dev::StreamDevice *)dev::k_devm.get_device(dev_);
			if (sdev == nullptr)
			{
				printfRed("file write: null device for device number %d", dev_);
				return -1;
			}

			if (sdev->type() != dev::dev_char)
			{
				printfRed("file write: device %d is not a char-dev", dev_);
				return -1;
			}

			if (!sdev->support_stream())
			{
				printfRed("file write: device %d is not a stream-dev", dev_);
				return -1;
			}

			ret = sdev->read((void *)dst_, len_);

			return ret;
		}

		size_t Device::nodeWrite(uint64 src_, size_t off_, size_t len_)
		{
			int ret;

			dev::StreamDevice *sdev = (dev::StreamDevice *)dev::k_devm.get_device(dev_);
			if (sdev == nullptr)
			{
				printfRed("file write: null device for device number %d", dev_);
				return -1;
			}

			if (sdev->type() != dev::dev_char)
			{
				printfRed("file write: device %d is not a char-dev", dev_);
				return -1;
			}

			if (!sdev->support_stream())
			{
				printfRed("file write: device %d is not a stream-dev", dev_);
				return -1;
			}

			ret = sdev->write((void *)src_, len_);

			return ret;
		}

		size_t Zero::nodeRead(uint64 src_, size_t off, size_t len)
		{
			memset((void *)src_, 0, len);
			return len;
		}

		size_t Normal::nodeRead(uint64 src_, size_t off, size_t len)
		{
			size_t read_len;
			[[maybe_unused]] size_t len_ = data.length();
			if (off + len > len_)
				read_len = len_ - off;
			else
				read_len = len;
			if (read_len == 0)
				return 0;

			char *readcontent = new char[read_len];
			memset(readcontent, 0, read_len);
			for (size_t i = 0; i < read_len; ++i)
			{
				readcontent[i] = data[off + i];
			}

			memcpy((void *)src_, readcontent, read_len);
			return read_len;
		}

		size_t Normal::nodeWrite(uint64 src_, size_t off, size_t len)
		{
			size_t write_len = len;
			char *writecontent = new char[len];
			memset(writecontent, 0, len);
			// printfRed("i try to print *src:%p,char:%s\n",src_,(char*)src_);
			memcpy(writecontent, (void *)src_, len);
			// printf("test.txt write: %s\n", writecontent);
			for (size_t i = 0; i < write_len; ++i)
			{
				if (off + i < data.size())
				{
					data[off + i] = writecontent[i];
				}
				else
				{
					data.push_back(writecontent[i]);
				}
			}
			// printf("data is %s\n", data.c_str());
			// printf("data size is %d\n", data.length());
			return write_len;
		}
	}
}
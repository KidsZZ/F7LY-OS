//
// Copy from Li Shuang ( pseudonym ) on 2024-07-16
// --------------------------------------------------------------
// | Note: This code file just for study, not for commercial use
// | Contact Author: lishuang.mk@whu.edu.cn
// --------------------------------------------------------------
//

#include "device_manager.hh"
#include "printer.hh"
#include "virtual_device.hh"

#include "klib.hh"

namespace dev
{
	DeviceManager k_devm;
	ConsoleStdin k_stdin;
	ConsoleStdout k_stdout;
	ConsoleStderr k_stderr;
	const char *DeviceManager::_device_default_name = "(null device)";

	DeviceManager::DeviceManager()
	{
		for (auto &te : _device_table)
			_reset_device_table_entry(&te);
		_device_table[DEV_STDIN_NUM].device_name = "stdin";
		_device_table[DEV_STDOUT_NUM].device_name = "stdout";
		_device_table[DEV_STDERR_NUM].device_name = "stderr";
		for (int i = 0; i < DEV_TBL_LEN; ++i)
		{
			if (_device_table[i].device_ptr != nullptr)
			{
				Info("  [%d] %s\n", i, _device_table[i].device_name);
				printfBlue("构造函数");
			}
		}
	}

	int DeviceManager::register_device(VirtualDevice *dev, const char *name)
	{
		int tei = _search_null_device();
		if (tei < 0)
			return tei;
		DeviceTableEntry &te = _device_table[tei];
		te.device_ptr = dev;
		te.device_name = name;
		Info("\e[5m"
			 "register device %s to No.%d\n",
			 name, tei);
		return tei;
	}

	int DeviceManager::register_block_device(BlockDevice *bd, const char *name)
	{
		return register_device((VirtualDevice *)bd, name);
	}

	int DeviceManager::register_char_device(CharDevice *cd, const char *name)
	{
		return register_device((VirtualDevice *)cd, name);
	}

	VirtualDevice *DeviceManager::get_device(const char *dev_name)
	{
		int dev_num = search_device(dev_name);
		if (dev_num < 0)
			return nullptr;
		return _device_table[dev_num].device_ptr;
	}

	BlockDevice *DeviceManager::get_block_device(const char *dev_name)
	{
		int dev_num = search_block_device(dev_name);
		if (dev_num < 0)
			return nullptr;
		return (BlockDevice *)_device_table[dev_num].device_ptr;
	}

	CharDevice *DeviceManager::get_char_device(const char *dev_name)
	{
		int dev_num = search_char_device(dev_name);
		if (dev_num < 0)
			return nullptr;
		return (CharDevice *)_device_table[dev_num].device_ptr;
	}

	int DeviceManager::search_device(const char *name)
	{
		for (int i = 0; i < DEV_TBL_LEN; ++i)
		{
			DeviceTableEntry &te = _device_table[i];
			if (te.device_ptr == nullptr)
				continue;
			if (te.device_ptr->type() == dev_unknown)
				continue;
			if (strcmp(name, te.device_name) == 0)
				return i;
		}
		return -1;
	}

	int DeviceManager::search_block_device(const char *name)
	{
		for (int i = DEV_FIRST_NOT_RSV; i < DEV_TBL_LEN; ++i)
		{
			DeviceTableEntry &te = _device_table[i];
			if (te.device_ptr == nullptr)
				continue;
			if (te.device_ptr->type() != dev_block)
				continue;
			if (strcmp(name, te.device_name) == 0)
				return i;
		}
		return -1;
	}

	int DeviceManager::search_char_device(const char *name)
	{
		for (int i = DEV_FIRST_NOT_RSV; i < DEV_TBL_LEN; ++i)
		{
			DeviceTableEntry &te = _device_table[i];
			if (te.device_ptr == nullptr)
				continue;
			if (te.device_ptr->type() != dev_char)
				continue;
			if (strcmp(name, te.device_name) == 0)
				return i;
		}
		return -1;
	}

	int DeviceManager::remove_block_device(const char *name)
	{
		for (int i = DEV_FIRST_NOT_RSV; i < DEV_TBL_LEN; ++i)
		{
			DeviceTableEntry &te = _device_table[i];
			if (te.device_ptr == nullptr)
				continue;
			if (te.device_ptr->type() != dev_char)
				continue;
			if (strcmp(name, te.device_name) == 0)
			{
				_reset_device_table_entry(&te);
				return i;
			}
		}
		return -1;
	}

	int DeviceManager::remove_char_device(const char *name)
	{
		for (int i = DEV_FIRST_NOT_RSV; i < DEV_TBL_LEN; ++i)
		{
			DeviceTableEntry &te = _device_table[i];
			if (te.device_ptr == nullptr)
				continue;
			if (te.device_ptr->type() != dev_char)
				continue;
			if (strcmp(name, te.device_name) == 0)
			{
				_reset_device_table_entry(&te);
				return i;
			}
		}
		return -1;
	}

	void DeviceManager::traversal_dev_table(char **dev_table)
	{
		for (int i = 0; i < DEV_TBL_LEN; ++i)
		{
			DeviceTableEntry &te = _device_table[i];
			if (te.device_ptr == nullptr)
				continue;
			dev_table[i] = (char *)te.device_name;
		}
	}
	// **************** private helper fuctions ****************

} // namespace dev

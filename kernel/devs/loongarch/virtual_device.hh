//
// Copied from Li Shuang ( pseudonym ) on 2024-07-19 
// --------------------------------------------------------------
// | Note: This code file just for study, not for commercial use 
// | Contact Author: lishuang.mk@whu.edu.cn 
// --------------------------------------------------------------
//

#pragma once

namespace dev
{
	enum DeviceType
	{
		dev_unknown,
		dev_block,
		dev_char,
		dev_other
	};

	class VirtualDevice
	{
	public:
		VirtualDevice() = default;
		virtual ~VirtualDevice() = default;
		virtual DeviceType type() { return DeviceType::dev_unknown; };
		virtual int handle_intr() = 0;
		virtual bool read_ready() = 0;
		virtual bool write_ready() = 0;
	};

} // namespace hsai

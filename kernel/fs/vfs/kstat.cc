//
// Copy from Li Shuang ( pseudonym ) on 2024-05-19 
// --------------------------------------------------------------
// | Note: This code file just for study, not for commercial use 
// | Contact Author: lishuang.mk@whu.edu.cn 
// --------------------------------------------------------------
//

#include "fs/vfs/kstat.hh"
#include "fs/vfs/fs.hh"

namespace fs
{
	// Kstat::Kstat( Dentry *dentry ) :
	// 	dev( dentry->getNode()->rDev() ),
	// 	ino( dentry->getNode()->rIno() ),
	// 	nlink( 1 ),
	// 	mode( dentry->getNode()->rMode() ),
	// 	uid( 0 ), gid( 0 ), rdev( 0 ), __pad( 0 ),
	// 	size( dentry->getNode()->rFileSize() ),
	// 	blksize( dentry->getNode()->getSb()->getFileSystem()->rBlockSize() ),
	// 	__pad2( 0 ), blocks( size / blksize )
	// {
	//};
} // namespace fs

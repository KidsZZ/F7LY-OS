//
// Copied from Li Shuang ( pseudonym ) on 2024-07-25 
// --------------------------------------------------------------
// | Note: This code file just for study, not for commercial use 
// | Contact Author: lishuang.mk@whu.edu.cn 
// --------------------------------------------------------------
//

#include "console1.hh"
#include "printer.hh"

namespace dev
{
// >>>> Console - STDIN

	long ConsoleStdin::write( void *, long )
	{
		printfYellow( "try to write stdin device" );
		return -1;
	}

	long ConsoleStdin::read( void * dst, long nbytes )
	{
		if ( _stream == nullptr )
		{
			printfYellow( "stream not be bound" );
			return 0;
		}
		u8 * ptr = ( u8 * ) dst;
				printfCyan("[consle]read before: %p",dst);
		for ( long i = 0; i < nbytes; i++, ptr++ )
			if ( _stream->get_char( ptr ) < 0 )
				return i;
		printfCyan("[consle]read: %s",dst);
		return nbytes;
	}

// <<<< Console - STDIN

// >>>> Console - STDOUT

	long ConsoleStdout::write( void * src, long nbytes )
	{
		if ( _stream == nullptr )
		{
			printfYellow( "未绑定流" );
			return 0;
		}
		u8 * ptr = ( u8 * ) src;
		for ( long i = 0; i < nbytes; i++, ptr++ )
			if ( _stream->put_char_sync( *ptr ) < 0 )
				return i;
		return nbytes;
	}

	long ConsoleStdout::read( void *, long )
	{
		printfYellow( "try to read stdout device" );
		return -1;
	}

// <<<< Console - STDOUT

// >>>> Console - STDERR

	long ConsoleStderr::write( void * src, long nbytes )
	{
		if ( _stream == nullptr )
		{
			printfYellow( "stream not be bound" );
			return 0;
		}
		u8 * ptr = ( u8 * ) src;
		for ( long i = 0; i < nbytes; i++, ptr++ )
			if ( _stream->put_char_sync( *ptr ) < 0 )
				return i;
		return nbytes;
	}

	long ConsoleStderr::read( void *, long )
	{
		printfYellow( "try to read stdout device" );
		return -1;
	}

// <<<< Console - STDERR

} 

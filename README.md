* Compiles with VS2012
* Needs latest VS2012 update for v110_xp toolset

Building libxml2 on Windows
---------------------------

The source is configured using the 'configure.js' configuration script.
This configuration script is executed using cscript utility.

The source is compiled using Microsoft's MSVC compiler using the nmake utility.

More information can be found in:
	libxml2-2.7.8/win32/Readme.txt

1. Open a Visual Studio command prompt.
2. Change directory to the win32 folder located in the libxml2-2.7.8 directory.

    As an example, type the following at the command prompt: `cd c:\libimobiledevice-win32\libxml2-2.7.8\win32`
	
3. Configure the source by choosing a build configuration:
   * **Release build** configuration

        Type the following at the command prompt: `cscript configure.js compiler=msvc iconv=no zlib=no cruntime=/MTd debug=yes`
 * **Debug build** configuration

        Type the following at the command prompt: `cscript configure.js compiler=msvc iconv=no zlib=no cruntime=/MT debug=no`
	
4. Compiling

    Type the following at the command prompt: `nmake /f Makefile.msvc`

When the building completes, you will find the necessary files in **win32\bin.msvc** directory.

If performing subsequent builds, you can choose to remove all compiler output files and return to a clean state by typing the following at the command prompt: `nmake /f Makefile.msvc clean`

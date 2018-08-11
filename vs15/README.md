Compilation instructions
===========
To compile the program in Visual Studio 2015 (newer versions should work as well), you first need to extract the files from https://osmocom.org/attachments/2242/RelWithDebInfo.zip into a folder "rtl-sdr" parallel to the rtl_433_win folder.
It has to look like this

    rtl_433_win
        include
        src
        vs15
        <and so on>
    
    rtl-sdr
        x64
        <and so on>
    
Then simply open rtl_433_win/vs15/rtl_433.sln in Visual Studio and select between debug/release build options.

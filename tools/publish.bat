for /f "tokens=5" %%i in ('SubWCRev  ..\^|find "Last committed at revision"') do set svnVersion=%%i
echo %svnVersion%
set buildDate=%date:~0,4%%date:~5,2%%date:~8,2%
echo %buildDate%

set buildTag=TDS_%buildDate%_%svnVersion%
echo %buildTag%


md ..\..\TDS_PUBLISH\%buildTag%

copy /y ..\out\tds.exe ..\..\TDS_PUBLISH\%buildTag%
copy /y ..\out\tds.pdb ..\..\TDS_PUBLISH\%buildTag%
pause
# Writes <CONTENT_DIR>/filelist.txt : every file and folder in the .wuhb's content, one relative path per line
# (folders end with '/'). The runtime uses it for case-insensitive lookups and directory listings of the
# content, because listing /vol/content doesn't work on real hardware (Aroma).
# Usage : cmake -DCONTENT_DIR=<dir> -P gen_filelist.cmake
file(GLOB_RECURSE entries LIST_DIRECTORIES true RELATIVE "${CONTENT_DIR}" "${CONTENT_DIR}/*")
list(REMOVE_ITEM entries "filelist.txt")
list(SORT entries)
set(out "")
foreach(e IN LISTS entries)
	if(IS_DIRECTORY "${CONTENT_DIR}/${e}")
		string(APPEND out "${e}/\n")
	else()
		string(APPEND out "${e}\n")
	endif()
endforeach()
# only rewrite when it changed, so the .wuhb isn't repacked for nothing
set(path "${CONTENT_DIR}/filelist.txt")
if(EXISTS "${path}")
	file(READ "${path}" old)
	if(old STREQUAL out)
		return()
	endif()
endif()
file(WRITE "${path}" "${out}")

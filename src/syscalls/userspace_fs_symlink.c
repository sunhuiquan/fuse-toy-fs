#include "userspace_fs_calls.h"
#include "log.h"
#include <string.h>

/* 创建符号链接，可以为目录创建符号链接 */
int userspace_fs_symlink(const char *oldpath, const char *newpath)
{
	if (oldpath == NULL || newpath == NULL)
		return -1;
	if (strlen(oldpath) >= MAX_NAME || strlen(newpath) >= MAX_NAME)
		return -1;
	// 之所以可以为目录创建符号链接，是因为在要递归遍历目录的情况下，我们不递归符号链接目录即可，不会破坏树形结构。
	return 0;
}
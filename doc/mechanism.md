# Mechanism

### 1. 用户态

设计 FS 的时候，为了显得不那么玩具，我想要通过 linux VFS 接口，让 linux 支持。但是直接在 linux 内核中实在太过困难，所以我选择了 FUSE 来实现一个用户态的文件系统.那么什么是 FUSE 呢？
简而言之，它作为一个中间层，把内核对 VFS 接口的使用从内核态转发到用户态的我们实现的用户文件系统机制，然后用户文件系统把结果再通过 libfuse 发回内核，来完成对抽象文件系统(VFS)的实现。

### 2. 分层设计

  to do 这里有些不准确，而且写的太丑了重写

   1. disk layer —— 读写磁盘的驱动代码（这里是用文件模拟），逻辑块与物理块映射关系；向上一层日志层提供读写磁盘的API。
   2. log layer —— 向上一层block cache层提供写日志头信息块的接口，用于事务提交；向文件系统调用提供事务进出、事务批处理提交的功能。
   3. block cache layer —— 为数据读写和上一层inode层读写inode结构提高数据块缓存机制。
   4. inode cache layer —— 存储文件信息，被上一层路径层根据路径查找到inode结构，得到文件信息和数据块号。
   5. path layer —— 路径层，为上一层的我们自己文件系统的系统调用作为参数使用。
   6. FUSE system calls layer —— 我们自己定义的系统调用，实现上一层的libfuse接口。
   7. libfuse layer —— libfuse库作为中间层，监听上一层的VFS的请求，返回我们自己的结果。
   8. linux VFS 机制 —— linux使用的虚拟文件系统机制，为上一层的glibc标准库的文件系统调用提供对应文件系统的功能实现，比如对一个ext4 FS的文件操作，自然向下调用ext4 FS实现，如果是对我们的FUSE文件操作，那么就会进入下一层libfuse layer，让libfuse layer转发请求到我们用户态的实现。
   9. glibc FS system calls layer —— 标准库的文件系统调用函数，不知要对哪一个文件系统调用。
   10. 打开文件描述 layer —— 指向inode，linux内核维护的信息。
   11. 文件描述符 layer —— 指向打开文件描述，linux内核维护的信息。  
  （注：为了简单，我们实现的是high-lever libfuse接口，使用路径；另外fd的层次是高于VFS的，也就是说打开不同文件系统而来的fd和相同FS的打开得到的fd没有什么不同，都是顺序递增，不可能重复的，由内核维护，fd和FUSE一点关系都没有）

### 3. 并发支持

   1. 通过pthread的mutex作为使用的锁机制，保证临界区的原子性访问，分别对于缓存区整体、每一个缓存区中的元素拥有一个锁，建立两层锁机制。例如第一层锁是对于inode cache整体有一个锁保证了一个 inode 在缓存只有一个副本(因为to do)，以及缓存 inode 的引用计数正确（to do）；第二层锁是对每个内存中的 inode 都有一个锁，保证了可以独占访问 inode 的相关字段，以及 inode 所对应的数据块（to do）。对于数据块整体 cache 结构和内存中的单个数据块结构同理，也是这样的二层锁机制保证。
   2. 另外我们通过两层引用计数完成了分别对内存中的缓存结构和磁盘中的具体块的释放复用。例如一个内存中的 inode 的引用计数如果大于 0，则会使系统将该 inode 保留在缓存中，而不会重用该缓存 buffer；而每个磁盘上的 inode 结构都包含一个 nlink 硬链接计数字段，即引用该 inode 结构的目录项的数量，当 inode 的硬链接计数为零时，才会释放磁盘上这一个 inode 结构，让它被复用。数据块则是只有一个内存中的引用计数字段保证数据块缓存结构中的释放复用，磁盘上的数据块只是单纯的数据。
   3. logging layer使用pthread mutex互斥锁，保证原子性的情况下，使用pthread cond条件变量避免轮询加解锁判断条件，提高性能。
   4. logging layer会增加block_cache的要写入缓存块元素的引用计数，并且在写入磁盘后才减引用计数，通过这一个来使得在block cache layer调用写系统调用与logging layer真正写磁盘的窗口期之间，不会产生因复用缓存块导致的竞争条件。
   5. 避免死锁，注意我们内部的通过path查找inode的两个函数 find_dir_inode() 和 find_path_inode()，内部都是按照从根路径向末尾加锁的顺序来对每一级的目录的inode加锁，按照这个顺序，我们可以保证并发查找inode的时候不会发生死锁。
   6. to do

### 4. 日志机制

   1. 写文件系统调用需要写入磁盘（或其他外存设备或者网络存储）上的内容，会改变外存存储的元数据和普通数据，而在软件崩溃（如OS内核崩溃）或者硬件崩溃（如断电）很有可能造成存储的元数据和普通数据的不一致问题，日志恢复的核心工作就是处理这一个数据不一致的问题，避免导致严重错误。
   2. 日志恢复本身的原理 to do
   3. 写文件系统调用使用 in_transaction( )和 out_transaction() 进出事务，注意并不是一个写系统调用一个事务，而是一个事务里面可以包含多个写系统调用，采用这种批处理的方式，目的是提高性能，当之后当日志块容量不足或者目前无写系统调用被调用的时候，一起提交写入日志，然后根据之前写入磁盘头日志块的映射关系，把内存中的缓存块写入磁盘普通日志块，然后再把磁盘日志块内容写入磁盘数据块，完成事务提交。
   4. to do
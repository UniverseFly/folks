// fs.h: File System

#pragma once

#include "sfs/disk.h"

#include <_types/_uint32_t.h>
#include <cstdint>
#include <functional>
#include <sys/_types/_ssize_t.h>
#include <vector>

class FileSystem {
public:
  const static uint32_t MAGIC_NUMBER = 0xf0f03410;
  const static uint32_t INODES_PER_BLOCK = 128;
  const static uint32_t POINTERS_PER_INODE = 5;
  const static uint32_t POINTERS_PER_BLOCK = 1024;

private:
  struct SuperBlock {     // Superblock structure
    uint32_t MagicNumber; // File system magic number
    uint32_t Blocks;      // Number of blocks in file system
    uint32_t InodeBlocks; // Number of blocks reserved for inodes
    uint32_t Inodes;      // Number of inodes in file system
  };

  struct Inode {
    uint32_t Valid;                      // Whether or not inode is valid
    uint32_t Size;                       // Size of file
    uint32_t Direct[POINTERS_PER_INODE]; // Direct pointers
    uint32_t Indirect;                   // Indirect pointer
  };

  union Block {
    SuperBlock Super;                      // Superblock
    Inode Inodes[INODES_PER_BLOCK];        // Inode block
    uint32_t Pointers[POINTERS_PER_BLOCK]; // Pointer block
    char Data[Disk::BLOCK_SIZE];           // Data block
  };

  // TODO: Internal helper functions
  Disk *getDisk() const { return disk; }
  
  static SuperBlock getSuperblock(Disk *disk) {
    Block block;
    disk->read(0, block.Data);
    return block.Super;
  }

  SuperBlock getSuperblock() const {
    return getSuperblock(disk);
  }

  uint32_t getInodeBlkIndex(uint32_t inumber) const {
    return inumber / INODES_PER_BLOCK + 1;
  }

  Inode &getInode(uint32_t inumber, Block &inodeBlock) const {
    uint32_t inodeBlkIndex = getInodeBlkIndex(inumber);
    uint32_t offset = inumber % INODES_PER_BLOCK;
    disk->read(inodeBlkIndex, inodeBlock.Data);
    return inodeBlock.Inodes[offset];
  }

  /// return the disk block index for a given inode block index
  uint32_t getDiskBlkNo(const Inode &inode, uint32_t blockIndex) {
    if (blockIndex < 5) {
      return inode.Direct[blockIndex];
    }  else {
      Block indirectBlk;
      disk->read(inode.Indirect, indirectBlk.Data);
      return indirectBlk.Pointers[blockIndex - 5];
    }
  }

  void setDiskBlkNo(Inode &inode, uint32_t blkIndex, uint32_t value) {}

  std::vector<uint32_t> allocateBlocks(uint32_t count) {
    
  }

  void initFreeBlocks_forInodeBlock(const Inode (&inodes)[INODES_PER_BLOCK]);

  // TODO: Internal member variables
  Disk *disk = nullptr;
  // Bitmap for freeblocks, true indicating free
  std::vector<bool> freeBlocks;

public:
  static void debug(Disk *disk);
  static bool format(Disk *disk);

  bool mount(Disk *disk);

  ssize_t create();
  bool remove(size_t inumber);
  ssize_t stat(size_t inumber);

  ssize_t read(size_t inumber, char *data, size_t length, size_t offset);
  ssize_t write(size_t inumber, char *data, size_t length, size_t offset);
};

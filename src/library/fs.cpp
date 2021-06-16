// fs.cpp: File System

#include "sfs/fs.h"
#include "sfs/disk.h"

#include <_types/_uint32_t.h>
#include <algorithm>

#include <assert.h>
#include <cstdio>
#include <cstring>
#include <stdio.h>
#include <string.h>
#include <sys/_types/_size_t.h>
#include <vector>

// Debug file system -----------------------------------------------------------

void FileSystem::debug(Disk *disk) {
  Block block;

  // Read Superblock
  disk->read(0, block.Data);

  printf("SuperBlock:\n");
  printf("    magic number is %s\n", block.Super.MagicNumber == MAGIC_NUMBER ? "valid" : "invalid");
  printf("    %u blocks\n"         , block.Super.Blocks);
  printf("    %u inode blocks\n"   , block.Super.InodeBlocks);
  printf("    %u inodes\n"         , block.Super.Inodes);

  // The total number of Inode blocks
  const uint32_t inodeBlocks = block.Super.InodeBlocks;
  const uint32_t inodeCount = block.Super.Inodes;
  for (uint32_t i = 0; i != inodeBlocks; ++i) {
    // +1 cuz inode blocks start from 1
    disk->read(i + 1, block.Data);
    for (uint32_t inodeIndex = 0; inodeIndex != INODES_PER_BLOCK; ++inodeIndex) {
      // overall index over all inodes
      const auto inodeOverallIndex = i * INODES_PER_BLOCK + inodeIndex;
      // There wouldn't be any valid inodes
      if (i == inodeBlocks - 1 && inodeOverallIndex >= inodeCount) {
        break;
      }
      const auto &inode = block.Inodes[inodeIndex];
      if (inode.Valid == 1) {
        printf("Inode %u:\n", inodeOverallIndex);
        printf("    size: %u bytes\n", inode.Size);
        // The total number of blocks related to this inode
        // x + y - 1 / y == ceil(x/y)
        const uint32_t totalBlocks = (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
        // Here we only calculate the direct blocks. 5 here cuz for an inode block 5 ptrs are direct.
        if (totalBlocks <= 5) {
          // only direct blocks
          printf("    direct blocks:");
          for (uint32_t k = 0; k != totalBlocks; ++k) {
            printf(" %u", inode.Direct[k]);   
          }
          printf("\n");
        } else {
          // first print 5 direct blocks
          printf("    direct blocks: %u %u %u %u %u\n",
                 inode.Direct[0], inode.Direct[1], inode.Direct[2],
                 inode.Direct[3], inode.Direct[4]);
          // then print the indirect block
          printf("    indirect block: %u\n", inode.Indirect);
          // finally print all indirect blocks in this indirect block
          // k stands for the indirect block index, starting from 5
          // k + 5 != ... instead of k != ... - 5 cuz they're unsigned
          Block indirectBlock;
          disk->read(inode.Indirect, indirectBlock.Data);
          printf("    indirect data blocks:");
          for (uint32_t k = 0; k + 5 != totalBlocks; ++k) {
            printf(" %u", indirectBlock.Pointers[k]);
          }
          printf("\n");
        }
      }
    }
  }
}

// Format file system ----------------------------------------------------------

bool FileSystem::format(Disk *disk) {
  if (disk->mounted()) { return false; }
  // Write superblock
  Block superblock;
  superblock.Super.MagicNumber = MAGIC_NUMBER;
  superblock.Super.Blocks = disk->size();
  // ceiling
  superblock.Super.InodeBlocks = (disk->size() + 10 - 1) / 10;
  superblock.Super.Inodes = superblock.Super.InodeBlocks * INODES_PER_BLOCK;
  disk->write(0, superblock.Data);

  // Clear all other blocks
  Block emptyBlock;
  memset(&emptyBlock.Data, 0, sizeof(emptyBlock));
  // note the i+1 here, otherwise the index will exceed the array boundary.
  for (uint32_t i = 0; i + 1 < disk->size(); ++i) {
    disk->write(i + 1, emptyBlock.Data);
  }
  return true;
}

// Mount file system -----------------------------------------------------------

bool FileSystem::mount(Disk *disk) {
  if (disk->mounted()) { return false; }
  // Read superblock
  const auto superblock = getSuperblock(disk);
  if (superblock.MagicNumber != MAGIC_NUMBER) {
    return false;
  }

  // if # of blocks is zero, it must be wrong
  if (superblock.Blocks == 0) {
    return false;
  }
  
  // # of inodes and # of superblock.inodes should be consistent
  if (superblock.Inodes != superblock.InodeBlocks * INODES_PER_BLOCK) {
    return false;
  }

  // # of blocks must be > # of InodeBlocks
  if (superblock.Blocks < superblock.InodeBlocks) {
    return false;
  }

  // Set device and mount
  disk->mount();

  // Copy metadata
  this->disk = disk;
  
  // Allocate free block bitmap
  freeBlocks = std::vector<bool>(disk->size(), true);
  freeBlocks[0] = false;
  Block inodeBlock;
  for (uint32_t i = 0; i < superblock.InodeBlocks; ++i) {
    disk->read(i + 1, inodeBlock.Data);
    freeBlocks[i + 1] = false;
    initFreeBlocks_forInodeBlock(inodeBlock.Inodes);
  }

  return true;
}

void FileSystem::initFreeBlocks_forInodeBlock(const Inode (&inodes)[INODES_PER_BLOCK]) {
  const auto disk = getDisk();
  for (uint32_t i = 0; i < INODES_PER_BLOCK; ++i) {
    const auto &inode = inodes[i];
    if (inode.Valid == 1) {
      // The total number of blocks related to this inode
      // x + y - 1 / y == ceil(x/y)
      const uint32_t totalBlocks = (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
      // Here we only calculate the direct blocks. 5 here cuz for an inode block 5 ptrs are direct.
      if (totalBlocks == 0) {}
      else if (totalBlocks <= 5) {
        // only direct blocks
        for (uint32_t k = 0; k != totalBlocks; ++k) {
          freeBlocks[inode.Direct[k]] = false;
        }
      } else {
        freeBlocks[inode.Direct[0]] = false;
        freeBlocks[inode.Direct[1]] = false;
        freeBlocks[inode.Direct[2]] = false;
        freeBlocks[inode.Direct[3]] = false;
        freeBlocks[inode.Direct[4]] = false;
        freeBlocks[inode.Indirect] = false;

        // k stands for the indirect block index, starting from 5
        // k + 5 != ... instead of k != ... - 5 cuz they're unsigned
        Block indirectBlock;
        disk->read(inode.Indirect, indirectBlock.Data);
        for (uint32_t k = 0; k + 5 != totalBlocks; ++k) {
          freeBlocks[indirectBlock.Pointers[k]] = false;
        }
      }
    }
  }
}

// Create inode ----------------------------------------------------------------

ssize_t FileSystem::create() {
  // Locate free inode in inode table
  const auto disk = getDisk();
  const auto &superblock = getSuperblock();
  // Iterate through inode blocks, and then for each
  // block iterate through all inodes
  Block inodeBlock;
  for (uint32_t i = 0; i < superblock.InodeBlocks; ++i) {
    disk->read(i + 1, inodeBlock.Data);
    for (uint32_t j = 0; j < INODES_PER_BLOCK; ++j) {
      auto &inode = inodeBlock.Inodes[j];
      // Because inodes are all located at the start of the disk,
      // if we can find an invalid one it can be used for creation.
      if (inode.Valid == 0) {
        inode.Valid = 1;
        // make inode change persistent
        disk->write(i + 1, inodeBlock.Data);
        // the inumber
        return i * INODES_PER_BLOCK + j;
      }
    }
  }
  
  // Record inode if not found
  return -1;
}

// Remove inode ----------------------------------------------------------------

bool FileSystem::remove(size_t inumber) {
  // Load inode information
  Block inodeBlock;
  auto &inode = getInode(inumber, inodeBlock);
  if (inode.Valid == 0) { return false; }

  // The total number of blocks related to this inode
  // x + y - 1 / y == ceil(x/y)
  const uint32_t totalBlocks = (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
  // Here we only calculate the direct blocks. 5 here cuz for an inode block 5 ptrs are direct.
  if (totalBlocks == 0) {}
  else if (totalBlocks <= 5) {
    // free direct blocks
    for (uint32_t k = 0; k != totalBlocks; ++k) {
      freeBlocks[inode.Direct[k]] = true;
    }
  } else {
    // free indirect blocks
    freeBlocks[inode.Direct[0]] = true;
    freeBlocks[inode.Direct[1]] = true;
    freeBlocks[inode.Direct[2]] = true;
    freeBlocks[inode.Direct[3]] = true;
    freeBlocks[inode.Direct[4]] = true;
    freeBlocks[inode.Indirect] = true;

    // k stands for the indirect block index, starting from 5
    // k + 5 != ... instead of k != ... - 5 cuz they're unsigned
    Block indirectBlock;
    disk->read(inode.Indirect, indirectBlock.Data);
    for (uint32_t k = 0; k + 5 != totalBlocks; ++k) {
      freeBlocks[indirectBlock.Pointers[k]] = true;
    }
  }

  // Clear inode in inode table
  // No need to clean other fields since it's an invalid inode
  inode.Valid = 0;
  disk->write(getInodeBlkIndex(inumber), inodeBlock.Data);

  return true;
}

// Inode stat ------------------------------------------------------------------

ssize_t FileSystem::stat(size_t inumber) {
  // Load inode information
  Block inodeBlock;
  uint32_t inodeBlkIndex = inumber / INODES_PER_BLOCK + 1;
  uint32_t offset = inumber % INODES_PER_BLOCK;
  
  if (inodeBlkIndex > disk->size()) { return -1; }
  disk->read(inodeBlkIndex, inodeBlock.Data);
  if (inodeBlock.Inodes[offset].Valid == 1) {
    return inodeBlock.Inodes[offset].Size;
  }

  return -1;
}

// Read from inode -------------------------------------------------------------

ssize_t FileSystem::read(size_t inumber, char *data, size_t length, size_t offset) {
  // Load inode information
  Block inodeBlock;

  auto &inode = getInode(inumber, inodeBlock);
  if (inode.Valid == 0) {
    return -1;
  }
  
  // Adjust length
  if (offset >= inode.Size) {
    return -1;
  }

  length = length > inode.Size - offset ? inode.Size - offset : length;

  // Read block and copy to data
  Block buffer;
  char *currentData = data;
  
  uint32_t startBlk = offset / Disk::BLOCK_SIZE;
  uint32_t endBlk = (offset + length + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
  printf("START: %d, END: %d\n", startBlk, endBlk);

  Block indirectBlk;
  for (uint32_t i = startBlk; i < endBlk; ++i) {
    printf("HI I AM READ.\n");
    uint32_t diskBlkNo;
    if (i < 5) {
      diskBlkNo = getDiskBlkNo_direct(inode, i);
    } else {
      disk->read(inode.Indirect, indirectBlk.Data);
      diskBlkNo = getDiskBlkNo_indirect(indirectBlk.Pointers, i);
    }
    disk->read(diskBlkNo, buffer.Data);
    // For the last block do not copy all the data
    if (i == endBlk - 1) {
      strncpy(currentData, buffer.Data, length == Disk::BLOCK_SIZE ? length : length % Disk::BLOCK_SIZE);
    } else {
      strncpy(currentData, buffer.Data, Disk::BLOCK_SIZE);
      currentData += Disk::BLOCK_SIZE;
    }
  }
  
  return length;
}

// Write to inode --------------------------------------------------------------

ssize_t FileSystem::write(size_t inumber, char *data, size_t length, size_t offset) {
  // Load inode
  Block inodeBlock;

  auto &inode = getInode(inumber, inodeBlock);
  if (inode.Valid == 0) {
    return -1;
  }
  
  if (offset > inode.Size) {
    return -1;
  }
  
  uint32_t startBlk = offset / Disk::BLOCK_SIZE;
  uint32_t endBlk = (offset + length + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
  
  // Write block and copy to data
  uint32_t oldBlockCount = (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
  uint32_t newBlockCount = (offset + length + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;

  printf("START %d, END %d, OLDC %d, NEWC %d\n", startBlk, endBlk, oldBlockCount, newBlockCount);

  Block indBlk;
  bool indSuccesfullyAllocated = false;
  if (newBlockCount > oldBlockCount) {
    // need to allocate indirect block
    if (oldBlockCount <= 5 and newBlockCount > 5) {
      auto indirect = allocateBlocks(1);
      if (!indirect.empty()) {
        printf("INDIRECT: %d", indirect[0]);
        inode.Indirect = indirect[0];
        indSuccesfullyAllocated = true;
      }
    }
    auto blockIndices = allocateBlocks(newBlockCount - oldBlockCount);
    // need to rectify endBlk since maybe cannot allocate  all blocks
    printf("%d %d %d\n", newBlockCount, oldBlockCount, blockIndices.size());
    inode.Size = blockIndices.size() == newBlockCount - oldBlockCount ? offset + length : offset + blockIndices.size() * Disk::BLOCK_SIZE;
    printf("SIZE: %d, %d\n", offset + length, offset + blockIndices.size() * Disk::BLOCK_SIZE);
    // subtract the 'lost' blocks
    endBlk -= (newBlockCount - oldBlockCount - blockIndices.size());
    for (uint32_t i = 0; i < blockIndices.size(); ++i) {
      assert(blockIndices[i] != 0);
      auto blkIndex = i + oldBlockCount;
      if (blkIndex < 5) {
        setDiskBlkNo_direct(inode, blkIndex, blockIndices[i]);
      } else {
        setDiskBlkNo_indirect(indBlk.Pointers, blkIndex, blockIndices[i]);
      }
      printf("%d <- %d\n", i + oldBlockCount, blockIndices[i]);
      // auto d = getDiskBlkNo(inode, i + oldBlockCount);
      // printf("%d\n", d);
    }
  }
  
  auto currentData = data;
  for (uint32_t i = startBlk; i < endBlk; ++i) {
    printf("HEY: I AM WRITE, %d\n", i);
    int index;
    if (i < 5) {
      index = getDiskBlkNo_direct(inode, i);
    } else {
      index = getDiskBlkNo_indirect(indBlk.Pointers, i);
    }
    printf("INDEX %d\n", index);
    disk->write(index, currentData);
    currentData += Disk::BLOCK_SIZE;
  }

  if (indSuccesfullyAllocated or (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE >= 5) {
    disk->write(inode.Indirect, indBlk.Data);
  }
  disk->write(getInodeBlkIndex(inumber), inodeBlock.Data);

  return inode.Size - offset;
}

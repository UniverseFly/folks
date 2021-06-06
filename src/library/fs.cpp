// fs.cpp: File System

#include "sfs/fs.h"

#include <_types/_uint32_t.h>
#include <algorithm>

#include <assert.h>
#include <cstdio>
#include <stdio.h>
#include <string.h>
#include <sys/_types/_size_t.h>

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
        printf("    indirect data blocks:");
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

    // Free direct blocks

    // Free indirect blocks

    // Clear inode in inode table
    return true;
}

// Inode stat ------------------------------------------------------------------

ssize_t FileSystem::stat(size_t inumber) {
    // Load inode information
    return 0;
}

// Read from inode -------------------------------------------------------------

ssize_t FileSystem::read(size_t inumber, char *data, size_t length, size_t offset) {
    // Load inode information

    // Adjust length

    // Read block and copy to data
    return 0;
}

// Write to inode --------------------------------------------------------------

ssize_t FileSystem::write(size_t inumber, char *data, size_t length, size_t offset) {
    // Load inode
    
    // Write block and copy to data
    return 0;
}

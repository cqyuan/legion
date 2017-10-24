/* Copyright 2017 Stanford University, NVIDIA Corporation
 * Copyright 2017 Los Alamos National Laboratory
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "realm/realm_config.h"
#include "realm/runtime_impl.h"
#include "realm/deppart/inst_helper.h"
#include "realm/mem_impl.h"
#include "realm/inst_impl.h"

#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

namespace Realm {
  
    DiskMemory::DiskMemory(Memory _me, size_t _size, std::string _file)
      : MemoryImpl(_me, _size, MKIND_DISK, ALIGNMENT, Memory::DISK_MEM), file(_file)
    {
      printf("file = %s\n", _file.c_str());
      // do not overwrite an existing file
      fd = open(_file.c_str(), O_CREAT | O_EXCL | O_RDWR, 00777);
      assert(fd != -1);
      // resize the file to what we want
      int ret =	ftruncate(fd, _size);
#ifdef NDEBUG
      (void)ret;
#else
      assert(ret == 0);
#endif
      free_blocks[0] = _size;
    }

    DiskMemory::~DiskMemory(void)
    {
      close(fd);
      // attempt to delete the file
      unlink(file.c_str());
    }

    off_t DiskMemory::alloc_bytes(size_t size)
    {
      return alloc_bytes_local(size);
    }

    void DiskMemory::free_bytes(off_t offset, size_t size)
    {
      free_bytes_local(offset, size);
    }

    void DiskMemory::get_bytes(off_t offset, void *dst, size_t size)
    {
      // this is a blocking operation
      ssize_t amt = pread(fd, dst, size, offset);
#ifdef NDEBUG
      (void)amt;
#else
      assert(amt == (ssize_t)size);
#endif
    }

    void DiskMemory::put_bytes(off_t offset, const void *src, size_t size)
    {
      // this is a blocking operation
      ssize_t amt = pwrite(fd, src, size, offset);
#ifdef NDEBUG
      (void)amt;
#else
      assert(amt == (ssize_t)size);
#endif
    }

    void DiskMemory::apply_reduction_list(off_t offset, const ReductionOpUntyped *redop,
                        size_t count, const void *entry_buffer)
    {
    }

    void *DiskMemory::get_direct_ptr(off_t offset, size_t size)
    {
      return 0; // cannot provide a pointer for it.
    }

    int DiskMemory::get_home_node(off_t offset, size_t size)
    {
      return my_node_id;
    }

    FileMemory::FileMemory(Memory _me)
      : MemoryImpl(_me, 0 /*no memory space*/, MKIND_FILE, ALIGNMENT, Memory::FILE_MEM)
      , next_offset(0x12340000LL)  // something not zero for debugging
    {
      pthread_mutex_init(&vector_lock, NULL);
    }

    FileMemory::~FileMemory(void)
    {
      pthread_mutex_destroy(&vector_lock);
    }

    off_t FileMemory::alloc_bytes(size_t size)
    {
      // hand out incrementing offsets and never reuse them
      // fragile, but we need a way to map from offset -> index for remote
      //  writes at the moment
      off_t this_offset = __sync_fetch_and_add(&next_offset, size);
      return this_offset;
    }

    void FileMemory::free_bytes(off_t offset, size_t size)
    {
      // Do nothing in this function.
    }

    void FileMemory::get_bytes(off_t offset, void *dst, size_t size)
    {
      // map from the offset back to the instance index
      assert(offset < next_offset);
      pthread_mutex_lock(&vector_lock);
      // this finds the first entry _AFTER_ the one we want
      std::map<off_t, int>::const_iterator it = offset_map.upper_bound(offset);
      assert(it != offset_map.begin());
      // back up to the element we want
      --it;
      ID::IDType index = it->second;
      off_t rel_offset = offset - it->first;
      pthread_mutex_unlock(&vector_lock);
      get_bytes(index, rel_offset, dst, size);
    }

    void FileMemory::get_bytes(ID::IDType inst_id, off_t offset, void *dst, size_t size)
    {
      pthread_mutex_lock(&vector_lock);
      int fd = file_vec[inst_id];
      pthread_mutex_unlock(&vector_lock);
      size_t ret = pread(fd, dst, size, offset);
#ifdef NDEBUG
      (void)ret;
#else
      assert(ret == size);
#endif
    }

    void FileMemory::put_bytes(off_t offset, const void *src, size_t)
    {
      // map from the offset back to the instance index
      assert(offset < next_offset);
      pthread_mutex_lock(&vector_lock);
      // this finds the first entry _AFTER_ the one we want
      std::map<off_t, int>::const_iterator it = offset_map.upper_bound(offset);
      assert(it != offset_map.begin());
      // back up to the element we want
      --it;
      ID::IDType index = it->second;
      off_t rel_offset = offset - it->first;
      pthread_mutex_unlock(&vector_lock);
      put_bytes(index, rel_offset, src, size);
    }

    void FileMemory::put_bytes(ID::IDType inst_id, off_t offset, const void *src, size_t size)
    {
      pthread_mutex_lock(&vector_lock);
      int fd = file_vec[inst_id];
      pthread_mutex_unlock(&vector_lock);
      size_t ret = pwrite(fd, src, size, offset);
#ifdef NDEBUG
      (void)ret;
#else
      assert(ret == size);
#endif
    }

    void FileMemory::apply_reduction_list(off_t offset, const ReductionOpUntyped *redop,
                                          size_t count, const void *entry_buffer) {}

    void *FileMemory::get_direct_ptr(off_t offset, size_t size)
    {
      return 0; // cannot provide a pointer for it;
    }

    int FileMemory::get_home_node(off_t offset, size_t size)
    {
      return my_node_id;
    }

    int FileMemory::get_file_des(ID::IDType inst_id)
    {
      pthread_mutex_lock(&vector_lock);
      int fd = file_vec[inst_id];
      pthread_mutex_unlock(&vector_lock);
      return fd;
    }

  template <int N, typename T>
  /*static*/ Event RegionInstance::create_file_instance(RegionInstance& inst,
							const char *file_name,
							const IndexSpace<N,T>& space,
							const std::vector<FieldID> &field_ids,
							const std::vector<size_t> &field_sizes,
							realm_file_mode_t file_mode,
							const ProfilingRequestSet& prs,
							Event wait_on /*= Event::NO_EVENT*/)
  {
    // look up the local file memory
    Memory memory = Machine::MemoryQuery(Machine::get_machine())
      .local_address_space()
      .only_kind(Memory::FILE_MEM)
      .first();
    assert(memory.exists());
    
    // construct an instance layout for the new instance
    // for now, we put the fields in order and use a fortran
    //  linearization
    InstanceLayout<N,T> *layout = new InstanceLayout<N,T>;
    layout->bytes_used = 0;
    layout->alignment_reqd = 0;  // no allocation being made
    layout->space = space;
    layout->piece_lists.resize(field_sizes.size());

    size_t file_ofs = 0;
    for(size_t i = 0; i < field_sizes.size(); i++) {
      FieldID id = field_ids[i];
      InstanceLayoutGeneric::FieldLayout& fl = layout->fields[id];
      fl.list_idx = i;
      fl.rel_offset = 0;
      fl.size_in_bytes = field_sizes[i];

      // create a single piece (for non-empty index spaces)
      if(!space.empty()) {
	AffineLayoutPiece<N,T> *alp = new AffineLayoutPiece<N,T>;
	alp->bounds = space.bounds;
	alp->offset = file_ofs;
	size_t stride = field_sizes[i];
	for(int j = 0; j < N; j++) {
	  alp->strides[j] = stride;
	  alp->offset -= space.bounds.lo[j] * stride;
	  stride *= (space.bounds.hi[j] - space.bounds.lo[j] + 1);
	}
	layout->piece_lists[i].pieces.push_back(alp);
	file_ofs += stride;
      }
    }

    // continue to support creating the file for now
    if(file_mode == LEGION_FILE_CREATE) {
      int fd = open(file_name, O_CREAT | O_RDWR, 0777);
      assert(fd != -1);
      // resize the file to what we want
      int ret = ftruncate(fd, file_ofs);
      assert(ret == 0);
      ret = close(fd);
      assert(ret == 0);
    }
    
    // and now create the instance using this layout
    Event e = create_instance(inst, memory, layout, prs, wait_on);

    // stuff the filename into the impl's metadata for now
    RegionInstanceImpl *impl = get_runtime()->get_instance_impl(inst);
    impl->metadata.filename = file_name;

    return e;
  }

  #define DOIT(N,T) \
  template Event RegionInstance::create_file_instance<N,T>(RegionInstance&, \
							   const char *, \
							   const IndexSpace<N,T>&, \
							   const std::vector<FieldID>&, \
							   const std::vector<size_t>&, \
                                                           realm_file_mode_t, \
							   const ProfilingRequestSet&, \
							   Event);
  FOREACH_NT(DOIT)



}


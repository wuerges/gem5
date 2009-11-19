/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/ruby/system/CacheMemory.hh"

int CacheMemory::m_num_last_level_caches = 0;
MachineType CacheMemory::m_last_level_machine_type = MachineType_FIRST;

// Output operator declaration
//ostream& operator<<(ostream& out, const CacheMemory<ENTRY>& obj);

// ******************* Definitions *******************

// Output operator definition
ostream& operator<<(ostream& out, const CacheMemory& obj)
{
  obj.print(out);
  out << flush;
  return out;
}


// ****************************************************************

CacheMemory::CacheMemory(const string & name)
  : m_cache_name(name)
{
  m_profiler_ptr = new CacheProfiler(name);
}

void CacheMemory::init(const vector<string> & argv)
{
  int cache_size = -1;
  string policy;

  m_num_last_level_caches = 
    MachineType_base_count(MachineType_FIRST);
  m_controller = NULL;
  for (uint32 i=0; i<argv.size(); i+=2) {
    if (argv[i] == "size") {
      cache_size = atoi(argv[i+1].c_str());
    } else if (argv[i] == "latency") {
      m_latency = atoi(argv[i+1].c_str());
    } else if (argv[i] == "assoc") {
      m_cache_assoc = atoi(argv[i+1].c_str());
    } else if (argv[i] == "replacement_policy") {
      policy = argv[i+1];
    } else if (argv[i] == "controller") {
      m_controller = RubySystem::getController(argv[i+1]);
      if (m_last_level_machine_type < m_controller->getMachineType()) {
        m_num_last_level_caches = 
          MachineType_base_count(m_controller->getMachineType());
        m_last_level_machine_type = 
          m_controller->getMachineType();
      } 
    } else {
      cerr << "WARNING: CacheMemory: Unknown configuration parameter: " << argv[i] << endl;
    }
  }

  assert(cache_size != -1);
  
  m_cache_num_sets = (cache_size / m_cache_assoc) / RubySystem::getBlockSizeBytes();
  assert(m_cache_num_sets > 1);
  m_cache_num_set_bits = log_int(m_cache_num_sets);
  assert(m_cache_num_set_bits > 0);

  if(policy == "PSEUDO_LRU")
    m_replacementPolicy_ptr = new PseudoLRUPolicy(m_cache_num_sets, m_cache_assoc);
  else if (policy == "LRU")
    m_replacementPolicy_ptr = new LRUPolicy(m_cache_num_sets, m_cache_assoc);
  else
    assert(false);

  m_cache.setSize(m_cache_num_sets);
  m_locked.setSize(m_cache_num_sets);
  for (int i = 0; i < m_cache_num_sets; i++) {
    m_cache[i].setSize(m_cache_assoc);
    m_locked[i].setSize(m_cache_assoc);
    for (int j = 0; j < m_cache_assoc; j++) {
      m_cache[i][j] = NULL;
      m_locked[i][j] = -1;
    }
  }
}

CacheMemory::~CacheMemory()
{
  if(m_replacementPolicy_ptr != NULL)
    delete m_replacementPolicy_ptr;
  delete m_profiler_ptr;
  for (int i = 0; i < m_cache_num_sets; i++) {
    for (int j = 0; j < m_cache_assoc; j++) {
      delete m_cache[i][j];
    }
  }
}

int
CacheMemory::numberOfLastLevelCaches() 
{ 
  return m_num_last_level_caches; 
}


void CacheMemory::printConfig(ostream& out)
{
  out << "Cache config: " << m_cache_name << endl;
  if (m_controller != NULL)
    out << "  controller: " << m_controller->getName() << endl;
  out << "  cache_associativity: " << m_cache_assoc << endl;
  out << "  num_cache_sets_bits: " << m_cache_num_set_bits << endl;
  const int cache_num_sets = 1 << m_cache_num_set_bits;
  out << "  num_cache_sets: " << cache_num_sets << endl;
  out << "  cache_set_size_bytes: " << cache_num_sets * RubySystem::getBlockSizeBytes() << endl;
  out << "  cache_set_size_Kbytes: "
      << double(cache_num_sets * RubySystem::getBlockSizeBytes()) / (1<<10) << endl;
  out << "  cache_set_size_Mbytes: "
      << double(cache_num_sets * RubySystem::getBlockSizeBytes()) / (1<<20) << endl;
  out << "  cache_size_bytes: "
      << cache_num_sets * RubySystem::getBlockSizeBytes() * m_cache_assoc << endl;
  out << "  cache_size_Kbytes: "
      << double(cache_num_sets * RubySystem::getBlockSizeBytes() * m_cache_assoc) / (1<<10) << endl;
  out << "  cache_size_Mbytes: "
      << double(cache_num_sets * RubySystem::getBlockSizeBytes() * m_cache_assoc) / (1<<20) << endl;
}

// PRIVATE METHODS

// convert a Address to its location in the cache
Index CacheMemory::addressToCacheSet(const Address& address) const
{
  assert(address == line_address(address));
  return address.bitSelect(RubySystem::getBlockSizeBits(), RubySystem::getBlockSizeBits() + m_cache_num_set_bits-1);
}

// Given a cache index: returns the index of the tag in a set.
// returns -1 if the tag is not found.
int CacheMemory::findTagInSet(Index cacheSet, const Address& tag) const
{
  assert(tag == line_address(tag));
  // search the set for the tags
  for (int i=0; i < m_cache_assoc; i++) {
    if ((m_cache[cacheSet][i] != NULL) &&
        (m_cache[cacheSet][i]->m_Address == tag) &&
        (m_cache[cacheSet][i]->m_Permission != AccessPermission_NotPresent)) {
      return i;
    }
  }
  return -1; // Not found
}

// Given a cache index: returns the index of the tag in a set.
// returns -1 if the tag is not found.
int CacheMemory::findTagInSetIgnorePermissions(Index cacheSet, const Address& tag) const
{
  assert(tag == line_address(tag));
  // search the set for the tags
  for (int i=0; i < m_cache_assoc; i++) {
    if (m_cache[cacheSet][i] != NULL && m_cache[cacheSet][i]->m_Address == tag)
      return i;
  }
  return -1; // Not found
}

// PUBLIC METHODS
bool CacheMemory::tryCacheAccess(const Address& address,
                                 CacheRequestType type,
                                 DataBlock*& data_ptr)
{
  assert(address == line_address(address));
  DEBUG_EXPR(CACHE_COMP, HighPrio, address);
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  if(loc != -1){ // Do we even have a tag match?
    AbstractCacheEntry* entry = m_cache[cacheSet][loc];
    m_replacementPolicy_ptr->touch(cacheSet, loc, g_eventQueue_ptr->getTime());
    data_ptr = &(entry->getDataBlk());

    if(entry->m_Permission == AccessPermission_Read_Write) {
      return true;
    }
    if ((entry->m_Permission == AccessPermission_Read_Only) &&
        (type == CacheRequestType_LD || type == CacheRequestType_IFETCH)) {
      return true;
    }
    // The line must not be accessible
  }
  data_ptr = NULL;
  return false;
}

bool CacheMemory::testCacheAccess(const Address& address,
                                  CacheRequestType type,
                                  DataBlock*& data_ptr)
{
  assert(address == line_address(address));
  DEBUG_EXPR(CACHE_COMP, HighPrio, address);
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  if(loc != -1){ // Do we even have a tag match?
    AbstractCacheEntry* entry = m_cache[cacheSet][loc];
    m_replacementPolicy_ptr->touch(cacheSet, loc, g_eventQueue_ptr->getTime());
    data_ptr = &(entry->getDataBlk());

    return (m_cache[cacheSet][loc]->m_Permission != AccessPermission_NotPresent);
  }
  data_ptr = NULL;
  return false;
}

// tests to see if an address is present in the cache
bool CacheMemory::isTagPresent(const Address& address) const
{
  assert(address == line_address(address));
  Index cacheSet = addressToCacheSet(address);
  int location = findTagInSet(cacheSet, address);

  if (location == -1) {
    // We didn't find the tag
    DEBUG_EXPR(CACHE_COMP, LowPrio, address);
    DEBUG_MSG(CACHE_COMP, LowPrio, "No tag match");
    return false;
  }
  DEBUG_EXPR(CACHE_COMP, LowPrio, address);
  DEBUG_MSG(CACHE_COMP, LowPrio, "found");
  return true;
}

// Returns true if there is:
//   a) a tag match on this address or there is
//   b) an unused line in the same cache "way"
bool CacheMemory::cacheAvail(const Address& address) const
{
  assert(address == line_address(address));

  Index cacheSet = addressToCacheSet(address);

  for (int i=0; i < m_cache_assoc; i++) {
    AbstractCacheEntry* entry = m_cache[cacheSet][i];
    if (entry != NULL) {
      if (entry->m_Address == address ||                         // Already in the cache
          entry->m_Permission == AccessPermission_NotPresent) {  // We found an empty entry
        return true;
      }
    } else {
      return true;
    }
  }
  return false;
}

void CacheMemory::allocate(const Address& address, AbstractCacheEntry* entry)
{
  assert(address == line_address(address));
  assert(!isTagPresent(address));
  assert(cacheAvail(address));
  DEBUG_EXPR(CACHE_COMP, HighPrio, address);

  // Find the first open slot
  Index cacheSet = addressToCacheSet(address);
  for (int i=0; i < m_cache_assoc; i++) {
    if (m_cache[cacheSet][i] == NULL ||
        m_cache[cacheSet][i]->m_Permission == AccessPermission_NotPresent) {
      m_cache[cacheSet][i] = entry;  // Init entry
      m_cache[cacheSet][i]->m_Address = address;
      m_cache[cacheSet][i]->m_Permission = AccessPermission_Invalid;
      m_locked[cacheSet][i] = -1;

      m_replacementPolicy_ptr->touch(cacheSet, i, g_eventQueue_ptr->getTime());

      return;
    }
  }
  ERROR_MSG("Allocate didn't find an available entry");
}

void CacheMemory::deallocate(const Address& address)
{
  assert(address == line_address(address));
  assert(isTagPresent(address));
  DEBUG_EXPR(CACHE_COMP, HighPrio, address);
  Index cacheSet = addressToCacheSet(address);
  int location = findTagInSet(cacheSet, address);
  if (location != -1){
    delete m_cache[cacheSet][location];
    m_cache[cacheSet][location] = NULL;
    m_locked[cacheSet][location] = -1;
  }
}

// Returns with the physical address of the conflicting cache line
Address CacheMemory::cacheProbe(const Address& address) const
{
  assert(address == line_address(address));
  assert(!cacheAvail(address));

  Index cacheSet = addressToCacheSet(address);
  return m_cache[cacheSet][m_replacementPolicy_ptr->getVictim(cacheSet)]->m_Address;
}

// looks an address up in the cache
AbstractCacheEntry& CacheMemory::lookup(const Address& address)
{
  assert(address == line_address(address));
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  assert(loc != -1);
  return *m_cache[cacheSet][loc];
}

// looks an address up in the cache
const AbstractCacheEntry& CacheMemory::lookup(const Address& address) const
{
  assert(address == line_address(address));
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  assert(loc != -1);
  return *m_cache[cacheSet][loc];
}

AccessPermission CacheMemory::getPermission(const Address& address) const
{
  assert(address == line_address(address));
  return lookup(address).m_Permission;
}

void CacheMemory::changePermission(const Address& address, AccessPermission new_perm)
{
  assert(address == line_address(address));
  lookup(address).m_Permission = new_perm;
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  m_locked[cacheSet][loc] = -1; 
  assert(getPermission(address) == new_perm);
}

// Sets the most recently used bit for a cache block
void CacheMemory::setMRU(const Address& address)
{
  Index cacheSet;

  cacheSet = addressToCacheSet(address);
  m_replacementPolicy_ptr->touch(cacheSet,
                                 findTagInSet(cacheSet, address),
                                 g_eventQueue_ptr->getTime());
}

void CacheMemory::profileMiss(const CacheMsg & msg) 
{
  m_profiler_ptr->addStatSample(msg.getType(), msg.getAccessMode(), 
				msg.getSize(), msg.getPrefetch());
}

void CacheMemory::recordCacheContents(CacheRecorder& tr) const
{
  for (int i = 0; i < m_cache_num_sets; i++) {
    for (int j = 0; j < m_cache_assoc; j++) {
      AccessPermission perm = m_cache[i][j]->m_Permission;
      CacheRequestType request_type = CacheRequestType_NULL;
      if (perm == AccessPermission_Read_Only) {
        if (m_is_instruction_only_cache) {
          request_type = CacheRequestType_IFETCH;
        } else {
          request_type = CacheRequestType_LD;
        }
      } else if (perm == AccessPermission_Read_Write) {
        request_type = CacheRequestType_ST;
      }

      if (request_type != CacheRequestType_NULL) {
        //        tr.addRecord(m_chip_ptr->getID(), m_cache[i][j].m_Address,
        //                     Address(0), request_type, m_replacementPolicy_ptr->getLastAccess(i, j));
      }
    }
  }
}

void CacheMemory::print(ostream& out) const
{
  out << "Cache dump: " << m_cache_name << endl;
  for (int i = 0; i < m_cache_num_sets; i++) {
    for (int j = 0; j < m_cache_assoc; j++) {
      if (m_cache[i][j] != NULL) {
        out << "  Index: " << i
            << " way: " << j
            << " entry: " << *m_cache[i][j] << endl;
      } else {
        out << "  Index: " << i
            << " way: " << j
            << " entry: NULL" << endl;
      }
    }
  }
}

void CacheMemory::printData(ostream& out) const
{
  out << "printData() not supported" << endl;
}

void CacheMemory::clearStats() const
{
  m_profiler_ptr->clearStats();
}

void CacheMemory::printStats(ostream& out) const
{
  m_profiler_ptr->printStats(out);
}

void CacheMemory::getMemoryValue(const Address& addr, char* value,
                                 unsigned int size_in_bytes ){
  AbstractCacheEntry& entry = lookup(line_address(addr));
  unsigned int startByte = addr.getAddress() - line_address(addr).getAddress();
  for(unsigned int i=0; i<size_in_bytes; ++i){
    value[i] = entry.getDataBlk().getByte(i + startByte);
  }
}

void CacheMemory::setMemoryValue(const Address& addr, char* value,
                                 unsigned int size_in_bytes ){
  AbstractCacheEntry& entry = lookup(line_address(addr));
  unsigned int startByte = addr.getAddress() - line_address(addr).getAddress();
  assert(size_in_bytes > 0);
  for(unsigned int i=0; i<size_in_bytes; ++i){
    entry.getDataBlk().setByte(i + startByte, value[i]);
  }

  //  entry = lookup(line_address(addr));
}

void 
CacheMemory::setLocked(const Address& address, int context) 
{  
  assert(address == line_address(address));
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  assert(loc != -1);
  m_locked[cacheSet][loc] = context;
}

void 
CacheMemory::clearLocked(const Address& address) 
{
  assert(address == line_address(address));
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  assert(loc != -1);
  m_locked[cacheSet][loc] = -1;
}

bool
CacheMemory::isLocked(const Address& address, int context)
{
  assert(address == line_address(address));
  Index cacheSet = addressToCacheSet(address);
  int loc = findTagInSet(cacheSet, address);
  assert(loc != -1);
  return m_locked[cacheSet][loc] == context; 
}


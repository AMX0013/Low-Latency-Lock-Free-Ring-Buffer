// 
// ring_Buff is header-only C library that can be used in Windows Kernel Mode
// 
// NOTE
// ====
//   ring_Buff is Lock-Free implementation of the Ring-Buffer.
//	 
//		1. Ring-Buffer is a FIFO queue if used as SPSC or MPSC.
//		2. Can be used also as MPMC, however multiple-consumers will consume elements
//		   unordered, and also each element will be consumed by exactly one consumer.
//		3. Can be used in asynchronious way (co-routine) by using poll functions.
//

#pragma once
/*This is a Windows-specific header file that provides data types, function prototypes, and definitions used for interfacing with the Windows NT kernel.
 It includes definitions for structures and constants used with threads, processes, memory management, security, and more. 
 For example, it defines the HANDLE type, which is used to represent a handle to an object in Windows.*/
#include <winnt.h>
/*This header file is part of the Windows API and it provides basic type definitions and macros. 
  It includes definitions for basic types like 
  	DWORD (a 32-bit unsigned integer), 
	BOOL (a boolean value), BYTE (a byte), 
	LPSTR (a pointer to a string), and more. 
  It also contains macros for manipulating these types, such as LOWORD and HIWORD (which retrieve the low and high-order words of a DWORD, respectively).*/
#include <minwindef.h>


/// Ring-Buffer data structure
/// 
/// Note: this is control structure, and buffer itself needs to be allocated
/// separately by the user. It can be as simple as an array of elements.
typedef struct tagNTRINGB {
	LONG next_write_pos;
	LONG last_write_pos;
	LONG next_read_pos;
	LONG last_read_pos;
	LONG pow2_buffer_count;

} ring_Buff, *ring_Buff_ptr;

/// <summary>
/// Stream Position in Ring-Buffer
/// 
/// Note: each thread performing reads/writes to the stream (ring-buffer),
/// needs its own local stream position.
/// 
/// You can have as many threads reading/writing as you like, since we support
/// MPMC. However keep in mind that multiple-consumers will receive elements
/// unordered, and also each element can only be read by one consumer, i.e.
/// we do not support pub-sub, and only queue semantics.
/// 
/// Note also that both synchronous and asynchronous access can be performed on
/// same ring-buffer. One thread may be writing synchronously, while other
/// thread might be reading asynchronously so that it can read multiple
/// streams (from whichever is ready).
/// </summary>
typedef struct tagNTRINGB_POS {
	ring_Buff volatile *p_ringb;
	LONG current_pos;
} NTRINGB_POS, *PNTRINGB_POS;


/// <summary>
/// Construct Ring-Buffer
/// </summary>
/// <param name="p_ringb">A pointer to global (or shared) variable holding ring-buffer</param>
/// <param name="pow2_buffer_count">Total count of elements in the ring-buffer. Must be power of 2!</param>
void ntringb_init(ring_Buff volatile *p_ringb, LONG pow2_buffer_count) {
	p_ringb->next_write_pos = -1;
	p_ringb->last_write_pos = -1;
	p_ringb->next_read_pos = -1;
	p_ringb->last_read_pos = -1;
	p_ringb->pow2_buffer_count = pow2_buffer_count;
}

/// <summary>
/// Contruct Stream Position in the Ring-Buffer
/// </summary>
/// <param name="p_ringb">A pointer to global (or shared) variable holding ring-buffer</param>
/// <param name="p_result">A pointer to local variable holding ring-buffer stream position</param>
void ntringb_pos_init(ring_Buff volatile* p_ringb, PNTRINGB_POS p_result) {
	p_result->p_ringb = p_ringb;
	p_result->current_pos = -1;
}

/// <summary>
/// Tell available space for writing
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>Count of spaces available</returns>
LONG ntringb_available_write(PNTRINGB_POS p_ringb_pos) {
	LONG available = 0;

	available = p_ringb_pos->p_ringb->pow2_buffer_count + p_ringb_pos->p_ringb->last_read_pos - p_ringb_pos->current_pos + 1;

	return available;
}

/// <summary>
/// Tell available space for reading
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>Count of spaces available</returns>
LONG ntringb_available_read(PNTRINGB_POS p_ringb_pos) {
	LONG available = 0;
	
	available = p_ringb_pos->p_ringb->last_write_pos - p_ringb_pos->current_pos + 1;
	
	return available;
}

/// <summary>
/// Begin writing one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>Index of the available space</returns>
LONG ntringb_begin_write(PNTRINGB_POS p_ringb_pos) {
	LONG buffer_pos = 0;
	p_ringb_pos->current_pos = InterlockedIncrement(&(p_ringb_pos->p_ringb->next_write_pos));
	
	while (ntringb_available_write(p_ringb_pos) < 1)
	{
		MemoryBarrier();
	}

	buffer_pos = p_ringb_pos->current_pos & (p_ringb_pos->p_ringb->pow2_buffer_count - 1);
	return buffer_pos;
}

/// <summary>
/// Begin reading one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>Index of the ready element</returns>
LONG ntringb_begin_read(PNTRINGB_POS p_ringb_pos) {
	LONG buffer_pos = 0;
	p_ringb_pos->current_pos = InterlockedIncrement(&(p_ringb_pos->p_ringb->next_read_pos));

	while (ntringb_available_read(p_ringb_pos) < 1)
	{
		MemoryBarrier();
	}

	buffer_pos = p_ringb_pos->current_pos & (p_ringb_pos->p_ringb->pow2_buffer_count - 1);
	return buffer_pos;
}

/// <summary>
/// Commit writing one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
void ntringb_commit_write(PNTRINGB_POS p_ringb_pos) {
	LONG last_write_pos;
	
	last_write_pos = p_ringb_pos->current_pos - 1;
	
	while (last_write_pos != InterlockedCompareExchange(
		&(p_ringb_pos->p_ringb->last_write_pos),
		last_write_pos + 1,
		last_write_pos))
	{}
}

/// <summary>
/// Commit reading one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
void ntringb_commit_read(PNTRINGB_POS p_ringb_pos) {
	LONG last_read_pos;
	
	last_read_pos = p_ringb_pos->current_pos - 1;
	
	while (last_read_pos != InterlockedCompareExchange(
		&(p_ringb_pos->p_ringb->last_read_pos),
		last_read_pos + 1,
		last_read_pos))
	{}
}

//
// Async
//

/// <summary>
/// Begin writing one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>Index of the available space</returns>
LONG ntringb_poll_begin_write(PNTRINGB_POS p_ringb_pos) {
	LONG buffer_pos = 0;
	p_ringb_pos->current_pos = InterlockedIncrement(&(p_ringb_pos->p_ringb->next_write_pos));
	
	buffer_pos = p_ringb_pos->current_pos & (p_ringb_pos->p_ringb->pow2_buffer_count - 1);
	return buffer_pos;
}

/// <summary>
/// Begin reading one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>Index of the ready element</returns>
LONG ntringb_poll_begin_read(PNTRINGB_POS p_ringb_pos) {
	LONG buffer_pos = 0;
	p_ringb_pos->current_pos = InterlockedIncrement(&(p_ringb_pos->p_ringb->next_read_pos));

	buffer_pos = p_ringb_pos->current_pos & (p_ringb_pos->p_ringb->pow2_buffer_count - 1);
	return buffer_pos;
}

/// <summary>
/// Tell if space is ready for writing 
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>TRUE if space can be written to, FALSE if need to wait, and try again later</returns>
BOOL ntringb_poll_write_ready(PNTRINGB_POS p_ringb_pos) {
	LONG available = 0;
	
	MemoryBarrier();

	available = ntringb_available_write(p_ringb_pos);

	return (0 < available);
}

/// <summary>
/// Tell if element is ready for reading
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>TRUE if element can be read from, FALSE if need to wait, and try again later</returns>
BOOL ntringb_poll_read_ready(PNTRINGB_POS p_ringb_pos) {
	LONG available = 0;
	
	MemoryBarrier();

	available = ntringb_available_read(p_ringb_pos);

	return (0 < available);
}

/// <summary>
/// Try commit writing one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>TRUE if write can be committed, FALSE if need to wait, and try again later</returns>
BOOL ntringb_poll_commit_write(PNTRINGB_POS p_ringb_pos) {
	LONG last_write_pos;
	
	last_write_pos = p_ringb_pos->current_pos - 1;
	
	return (last_write_pos == InterlockedCompareExchange(
		&(p_ringb_pos->p_ringb->last_write_pos),
		last_write_pos + 1,
		last_write_pos));
}

/// <summary>
/// Try commit reading one element
/// </summary>
/// <param name="p_ringb_pos">A pointer to local variable holding ring-buffer stream position</param>
/// <returns>TRUE if read can be committed, FALSE if need to wait, and try again later</returns>
BOOL ntringb_poll_commit_read(PNTRINGB_POS p_ringb_pos) {
	LONG last_read_pos;
	
	last_read_pos = p_ringb_pos->current_pos - 1;
	
	return (last_read_pos == InterlockedCompareExchange(
		&(p_ringb_pos->p_ringb->last_read_pos),
		last_read_pos + 1,
		last_read_pos));
}


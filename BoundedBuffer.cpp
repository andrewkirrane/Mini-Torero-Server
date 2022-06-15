/*
 * Implementation of the BoundedBuffer class.
 * Declaration for this class is in the header file (BoundedBuffer.hpp)
 */

#include <cstdio>
#include "BoundedBuffer.hpp"

/*
 * Constructor that sets the buffer capacity to the max size given
 * Buffer is initialized to an empty queue
 *
 * @param max_size the capacity of the buffer
 */
BoundedBuffer::BoundedBuffer(int max_size) {
	capacity = max_size;
	count = 0;
	head = 0;
	tail = 0;
}

/*
 * Get the first item in the buffer, then remove it
 *
 * @returns item First item in buffer
 */
int BoundedBuffer::getItem() {
	std::unique_lock<std::mutex> cv_lock(m); // aquire or wait for lock and shared mutex
	while(count == 0) {
		data_available.wait(cv_lock);
	}
	count -= 1;
	// save item, then pop out of buffer
	int item = this->buffer.front();
	buffer.pop();
	tail += 1;
	if(tail == capacity) {
		tail = 0;
	}
	// notify that there is buffer space available
	space_available.notify_one();
	cv_lock.unlock();
	
	return item;
}

/*
 * Add a new item to the end of the buffer
 *
 * @param new_item The item to be added to the buffer
 */
void BoundedBuffer::putItem(int new_item) {
	std::unique_lock<std::mutex> cv_lock(m); // aquire or wait for lock and shared mutex
	while(count == capacity) {
		space_available.wait(cv_lock);
	}
	count += 1;
	// push item into buffer
	buffer.push(new_item);
	head += 1;
	if(head == capacity) {
		head = 0;
	}
	// notify that there is data in the buffer
	data_available.notify_one();
	cv_lock.unlock();
}

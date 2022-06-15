#include <queue>
#include <mutex>
#include <condition_variable>

/*
 * Class representing a buffer with a fixed capacity
 *
 * Note that in C++, the header (i.e. hpp) file contains a declaration of the
 * class while the implementation of the constructors, destructors, and methods,
 * and given in an implementation (i.e. cpp) file.
 */
class BoundedBuffer {
	public:
		// public constructor
		BoundedBuffer(int max_size);
		
		// public member functions
		int getItem();
		void putItem(int new_item);

		int count;
		int head;
		int tail;

	private:
		// private member variables
		int capacity;
		std::queue<int> buffer;
		std::mutex m;
		std::condition_variable data_available;
		std::condition_variable space_available;
};

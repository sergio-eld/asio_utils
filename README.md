# ASIO Utils Library

This is a small header-only template library to provide utilities to ease some operations with ASIO. 
It implements a set of composed functions with signatures that comply with Universal Asynchronous Model.

The source code can be used as a reference for educational purposes. I hope it will
be helpful to understand the concept of continuations, non-blocking asynchronous and 
multithreading programming.

## How to use
- with CMake using `add_subdirectory`. Clone the project as a submodule with git 
  and add the folder as a subdirectory.
- install with CMake and use `find_package(eld::asio_utils CONFIG REQUIRED)` (not implemented yet)
- just copy the `include` folder to your project. Don't forget to have ASIO available as it is a dependency.

## Stable functions and classes
- *async_connection_attempt* - asynchronously tries to establish a connection
  (TCP only for now) provided with a timeout and number of attempts.
- *async_send_queue* and *make_async_send_queue* - persistent asynchronous send queue.
Serializes calls to `asio::async_write`, now you can make calls as many as you like. 
  `asyncSend` is also thread-safe.
  
## Contribute
ASIO is not an easy library to understand or to use. Though the concept of continuations
and Universal Asynchronous Model are fairly simple to understand, the API and implementation
is heavy templated and lacks of documentation. If you have suggestions to improve API and ways to
use the utility functions from this library, feel free to open an issue.
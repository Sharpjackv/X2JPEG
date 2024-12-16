#include <opencv2/opencv.hpp>
#include <opencv2/stitching.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h> // Added for capturing cursor
#include <csignal>
#include <mutex>
#include <string>
#include <sstream>
#include <condition_variable>
#include <algorithm>
#include <atomic>
#include <unordered_map>

typedef websocketpp::server<websocketpp::config::asio> server;

bool verbose = false;
Window window = 0;
double capture_multiplier = 2.0;

class ScreenShot {
public:
   ScreenShot(Display* display, Window window, int width, int height)
       : display_(display), window_(window), width_(width), height_(height), max_size_(60 * 1024), fps_(60), stop_(false),
         capture_time_(0), encode_time_(0), compress_time_(0), capture_count_(0), encode_count_(0), compress_count_(0), compress_cycles_(0) {
       capture_thread_ = std::thread(&ScreenShot::capture_loop, this);
   }

   ~ScreenShot() {
       {
           std::lock_guard<std::mutex> lock(mutex_);
           stop_ = true;
       }
       cv_.notify_all();
       capture_thread_.join();
   }

   std::vector<unsigned char> get_buffer() {
       std::lock_guard<std::mutex> lock(mutex_);
       return buffer_;
   }

   void set_max_size(int max_size_kb) {
       std::lock_guard<std::mutex> lock(mutex_);
       max_size_ = max_size_kb * 1024; // Convert KB to bytes
   }

   void set_fps(int fps) {
       std::lock_guard<std::mutex> lock(mutex_);
       fps_ = fps;
       cv_.notify_all();
   }

   int get_fps() {
       std::lock_guard<std::mutex> lock(mutex_);
       return fps_;
   }

   void report_performance() {
       while (!stop_) {
           std::this_thread::sleep_for(std::chrono::seconds(1));
           std::ostringstream oss;
           if (capture_count_ > 0) {
               oss << "Average capture time: " << (capture_time_ / capture_count_) << " ms, ";
           }
           if (encode_count_ > 0 && compress_count_ > 0) {
               long long total_encode_compress_time = (encode_time_ + compress_time_) / (encode_count_ + compress_count_);
               oss << "Average encode+compress time: " << total_encode_compress_time << " ms, ";
           }
           if (compress_count_ > 0) {
               oss << "Average image size: " << (compress_time_ / compress_count_) << " KB, ";
           }
           oss << "Capture FPS: " << (fps_ * capture_multiplier) << ", Sending FPS: " << fps_;
           std::cout << "\r" << oss.str() << std::flush; // Print carriage return separately
       }
   }

   Display* display_;
   Window window_;

private:
   void capture_loop() {
       int quality = 100; // Start with no compression for the first frame

       while (true) {
           auto loop_start = std::chrono::steady_clock::now();

           {
               std::unique_lock<std::mutex> lock(mutex_);
               cv_.wait_for(lock, std::chrono::duration<double, std::milli>(1000.0 / (fps_ * capture_multiplier)), [this] { return stop_; });
               if (stop_) break;
           }

           auto capture_start = std::chrono::steady_clock::now();
           XLockDisplay(display_);
           XImage* image = XGetImage(display_, window_, 0, 0, width_, height_, AllPlanes, ZPixmap);
           if (!image) {
               XUnlockDisplay(display_);
               std::cerr << "XGetImage failed" << std::endl;
               continue;
           }

           // Capture the cursor
           XFixesCursorImage* cursor_image = XFixesGetCursorImage(display_);
           if (cursor_image) {
               int cursor_x = cursor_image->x - cursor_image->xhot;
               int cursor_y = cursor_image->y - cursor_image->yhot;
               for (int cy = 0; cy < cursor_image->height; ++cy) {
                   for (int cx = 0; cx < cursor_image->width; ++cx) {
                       int img_x = cursor_x + cx;
                       int img_y = cursor_y + cy;
                       if (img_x >= 0 && img_x < width_ && img_y >= 0 && img_y < height_) {
                           unsigned long pixel = cursor_image->pixels[cy * cursor_image->width + cx];
                           if (pixel >> 24) { // Check alpha channel
                               XPutPixel(image, img_x, img_y, pixel);
                           }
                       }
                   }
               }
               XFree(cursor_image);
           }
           XUnlockDisplay(display_);

           auto capture_end = std::chrono::steady_clock::now();
           capture_time_ += std::chrono::duration_cast<std::chrono::milliseconds>(capture_end - capture_start).count();
           capture_count_++;

           auto encode_start = std::chrono::steady_clock::now();
           cv::Mat mat_image(height_, width_, CV_8UC4, image->data);
           auto encode_end = std::chrono::steady_clock::now();
           encode_time_ += std::chrono::duration_cast<std::chrono::milliseconds>(encode_end - encode_start).count();
           encode_count_++;

           auto compress_start = std::chrono::steady_clock::now();
           std::vector<unsigned char> buffer;
           std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
           cv::imencode(".jpg", mat_image, buffer, params);

           // Adjust quality based on the size of the previous frame
           if (buffer.size() > max_size_) {
               quality = std::max(1, quality - 5); // Reduce quality
           } else if (buffer.size() < max_size_ - 10 * 1024) {
               quality = std::min(100, quality + 5); // Increase quality if at least 10 KB under max_size_
           }

           auto compress_end = std::chrono::steady_clock::now();
           compress_time_ += std::chrono::duration_cast<std::chrono::milliseconds>(compress_end - compress_start).count();
           compress_count_++;

           {
               std::lock_guard<std::mutex> lock(mutex_);
               buffer_ = std::move(buffer);
           }

           XLockDisplay(display_);
           XDestroyImage(image);
           XUnlockDisplay(display_);

           auto loop_end = std::chrono::steady_clock::now();
           auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start).count();
           auto sleep_time = std::max(0.0, (1000.0 / (fps_ * capture_multiplier)) - static_cast<double>(elapsed_time));
           std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleep_time));
       }
   }

   int width_, height_;
   int max_size_;
   int fps_;
   bool stop_;
   std::vector<unsigned char> buffer_;
   std::mutex mutex_;
   std::condition_variable cv_;
   std::thread capture_thread_;
   std::atomic<long long> capture_time_;
   std::atomic<long long> encode_time_;
   std::atomic<long long> compress_time_;
   std::atomic<int> capture_count_;
   std::atomic<int> encode_count_;
   std::atomic<int> compress_count_;
   std::atomic<int> compress_cycles_;
};

server ws_server;
std::mutex clients_mutex;
std::vector<websocketpp::connection_hdl> clients;

// Function to translate JavaScript event.code to Xlib KeySym
KeySym translate_js_code_to_keysym(const std::string& code) {
   static const std::unordered_map<std::string, KeySym> key_map = {
       // Letters
       {"KeyA", XK_a}, {"KeyB", XK_b}, {"KeyC", XK_c}, {"KeyD", XK_d},
       {"KeyE", XK_e}, {"KeyF", XK_f}, {"KeyG", XK_g}, {"KeyH", XK_h},
       {"KeyI", XK_i}, {"KeyJ", XK_j}, {"KeyK", XK_k}, {"KeyL", XK_l},
       {"KeyM", XK_m}, {"KeyN", XK_n}, {"KeyO", XK_o}, {"KeyP", XK_p},
       {"KeyQ", XK_q}, {"KeyR", XK_r}, {"KeyS", XK_s}, {"KeyT", XK_t},
       {"KeyU", XK_u}, {"KeyV", XK_v}, {"KeyW", XK_w}, {"KeyX", XK_x},
       {"KeyY", XK_y}, {"KeyZ", XK_z},

       // Numbers
       {"Digit0", XK_0}, {"Digit1", XK_1}, {"Digit2", XK_2}, {"Digit3", XK_3},
       {"Digit4", XK_4}, {"Digit5", XK_5}, {"Digit6", XK_6}, {"Digit7", XK_7},
       {"Digit8", XK_8}, {"Digit9", XK_9},

       // Function keys
       {"F1", XK_F1}, {"F2", XK_F2}, {"F3", XK_F3}, {"F4", XK_F4},
       {"F5", XK_F5}, {"F6", XK_F6}, {"F7", XK_F7}, {"F8", XK_F8},
       {"F9", XK_F9}, {"F10", XK_F10}, {"F11", XK_F11}, {"F12", XK_F12},

       // Control keys
       {"Enter", XK_Return}, {"Space", XK_space}, {"Backspace", XK_BackSpace},
       {"Tab", XK_Tab}, {"Escape", XK_Escape}, {"CapsLock", XK_Caps_Lock},
       {"ShiftLeft", XK_Shift_L}, {"ShiftRight", XK_Shift_R},
       {"ControlLeft", XK_Control_L}, {"ControlRight", XK_Control_R},
       {"AltLeft", XK_Alt_L}, {"AltRight", XK_Alt_R},
       {"MetaLeft", XK_Super_L}, {"MetaRight", XK_Super_R},

       // Arrow keys
       {"ArrowLeft", XK_Left}, {"ArrowUp", XK_Up}, {"ArrowRight", XK_Right}, {"ArrowDown", XK_Down},

       // Special characters
       {"Minus", XK_minus}, {"Equal", XK_equal}, {"BracketLeft", XK_bracketleft}, {"BracketRight", XK_bracketright},
       {"Backslash", XK_backslash}, {"Semicolon", XK_semicolon}, {"Quote", XK_apostrophe}, {"Backquote", XK_grave},
       {"Comma", XK_comma}, {"Period", XK_period}, {"Slash", XK_slash},
       {"IntlBackslash", XK_less},

       // Other keys
       {"Insert", XK_Insert}, {"Delete", XK_Delete}, {"Home", XK_Home}, {"End", XK_End},
       {"PageUp", XK_Page_Up}, {"PageDown", XK_Page_Down}, {"NumLock", XK_Num_Lock},
       {"ScrollLock", XK_Scroll_Lock}, {"Pause", XK_Pause}, {"PrintScreen", XK_Print}
   };

   auto it = key_map.find(code);
   if (it != key_map.end()) {
       return it->second;
   }
   return NoSymbol;
}

// Function to send a keyboard input
void send_key(Display* display, Window window, KeySym keysym, bool press) {
   KeyCode keycode = XKeysymToKeycode(display, keysym);
   if (keycode == 0) return;

   XLockDisplay(display);
   // Only set input focus if streaming a specific window
   if (window != DefaultRootWindow(display)) {
       XSetInputFocus(display, window, RevertToParent, CurrentTime);
   }
   XTestFakeKeyEvent(display, keycode, press, CurrentTime);
   XFlush(display);
   XUnlockDisplay(display);
}

// Function to send a relative mouse movement
void send_mouse_move(Display* display, Window window, int x, int y) {
   XLockDisplay(display);
   // If streaming a specific window, adjust position relative to that window
   if (window != DefaultRootWindow(display)) {
       // Get window position
       Window root_return;
       int win_x, win_y;
       unsigned int width_return, height_return, border_width_return, depth_return;
       XGetGeometry(display, window, &root_return, &win_x, &win_y,
                    &width_return, &height_return, &border_width_return, &depth_return);
       // Move pointer relative to the window
       XWarpPointer(display, None, window, 0, 0, 0, 0, x, y);
   } else {
       // Move pointer relative to the screen
       XTestFakeRelativeMotionEvent(display, x, y, CurrentTime);
   }
   XFlush(display);
   XUnlockDisplay(display);
}

// Function to send a mouse button event
void send_mouse_button(Display* display, Window window, int button, bool press) {
   XLockDisplay(display);
   XTestFakeButtonEvent(display, button, press, CurrentTime);
   XFlush(display);
   XUnlockDisplay(display);
}

void broadcast_image(ScreenShot& screen) {
   std::vector<unsigned char> previous_buffer;
   while (true) {
       auto start_time = std::chrono::steady_clock::now();

       std::vector<unsigned char> jpeg_data = screen.get_buffer();
       if (!jpeg_data.empty()) {
           previous_buffer = jpeg_data;
       } else {
           jpeg_data = previous_buffer;
       }

       std::lock_guard<std::mutex> lock(clients_mutex);
       for (auto& hdl : clients) {
           try {
               ws_server.send(hdl, jpeg_data.data(), jpeg_data.size(), websocketpp::frame::opcode::binary);
           } catch (const websocketpp::exception& e) {
               std::cerr << "WebSocket error: " << e.what() << std::endl;
           }
       }

       auto end_time = std::chrono::steady_clock::now();
       auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
       auto sleep_time = std::max(0, (1000 / screen.get_fps()) - static_cast<int>(elapsed_time));
       std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
   }
}

void on_message(server* ws_server, websocketpp::connection_hdl hdl, ScreenShot& screen, const std::string& msg) {
   if (msg.rfind("q ", 0) == 0) { // Check if the message starts with "q "
       int new_max_size_kb;
       std::istringstream iss(msg.substr(2));
       if (iss >> new_max_size_kb) {
           screen.set_max_size(new_max_size_kb);
           std::cerr << "Updated max image size to " << new_max_size_kb << " KB" << std::endl;
       }
   } else if (msg.rfind("f ", 0) == 0) { // Check if the message starts with "f "
       int new_fps;
       std::istringstream iss(msg.substr(2));
       if (iss >> new_fps) {
           screen.set_fps(new_fps);
           std::cerr << "Updated frame rate to " << new_fps << " FPS" << std::endl;
       }
   } else if (msg.rfind("2+", 0) == 0) { // Handle keyboard events
       std::istringstream iss(msg.substr(2));
       std::string event_type, js_code;
       if (std::getline(iss, event_type, '+') && std::getline(iss, js_code, '+')) {
           bool press = (event_type == "1");
           KeySym keysym = translate_js_code_to_keysym(js_code);
           if (keysym != NoSymbol) {
               send_key(screen.display_, screen.window_, keysym, press);
           }
       }
   } else if (msg.rfind("1+", 0) == 0) { // Handle mouse movement
       std::istringstream iss(msg.substr(2));
       int x, y;
       char plus;
       if (iss >> x >> plus >> y) {
           send_mouse_move(screen.display_, screen.window_, x, y);
       }
   } else if (msg.rfind("3+", 0) == 0) { // Handle mouse button events
       std::istringstream iss(msg.substr(2)); // Skip "3+"
       std::string press_str, button_str;
       if (std::getline(iss, press_str, '+') && std::getline(iss, button_str, '+')) {
           int press = std::stoi(press_str);
           int button_code = std::stoi(button_str);
           int button = 0;
           if (button_code == 1) {
               button = 1; // Left button (X11 button 1)
           } else if (button_code == 0) {
               button = 3; // Right button (X11 button 3)
           }
           send_mouse_button(screen.display_, screen.window_, button, press == 1);
       }
   }
}

void signal_handler(int signal) {
   std::cerr << "Signal " << signal << " received, stopping WebSocket server..." << std::endl;
   ws_server.stop_listening();
   ws_server.stop();
}

int main(int argc, char* argv[]) {
   if (argc > 1 && std::string(argv[1]) == "-v") {
       verbose = true;
   }

   bool window_id_provided = false;
   for (int i = 1; i < argc; ++i) {
       if (std::string(argv[i]) == "-w" && i + 1 < argc) {
           std::istringstream iss(argv[i + 1]);
           iss >> std::hex >> window;
           window_id_provided = true;
       } else if (std::string(argv[i]) == "-c" && i + 1 < argc) {
           std::istringstream iss(argv[i + 1]);
           iss >> capture_multiplier;
       }
   }

   // Initialize Xlib for multi-threaded use
   if (!XInitThreads()) {
       std::cerr << "Unable to initialize Xlib for multi-threaded use" << std::endl;
       return 1;
   }

   std::signal(SIGINT, signal_handler);
   std::signal(SIGTERM, signal_handler);

   Display* display = XOpenDisplay(nullptr);
   if (!display) {
       std::cerr << "Unable to open X display" << std::endl;
       return 1;
   }

   if (!window_id_provided) {
       window = DefaultRootWindow(display);
   }

   XWindowAttributes gwa;
   if (!XGetWindowAttributes(display, window, &gwa)) {
       std::cerr << "Unable to get window attributes" << std::endl;
       XCloseDisplay(display);
       return 1;
   }
   int window_width = gwa.width;
   int window_height = gwa.height;

   ScreenShot screen(display, window, window_width, window_height);

   // Disable WebSocketPP logging
   ws_server.clear_access_channels(websocketpp::log::alevel::all);
   ws_server.clear_error_channels(websocketpp::log::elevel::all);

   ws_server.init_asio();

   // Set reuse address option
   ws_server.set_reuse_addr(true);

   ws_server.set_open_handler([&screen](websocketpp::connection_hdl hdl) {
       std::lock_guard<std::mutex> lock(clients_mutex);
       clients.push_back(hdl);
   });

   ws_server.set_close_handler([](websocketpp::connection_hdl hdl) {
       std::lock_guard<std::mutex> lock(clients_mutex);
       clients.erase(std::remove_if(clients.begin(), clients.end(),
           [&hdl](const websocketpp::connection_hdl& other) {
               return !other.owner_before(hdl) && !hdl.owner_before(other);
           }), clients.end());
   });

   ws_server.set_message_handler([&screen](websocketpp::connection_hdl hdl, server::message_ptr msg) {
       std::string payload = msg->get_payload();
       on_message(&ws_server, hdl, screen, payload);
   });

   ws_server.listen(10034);
   ws_server.start_accept();

   std::cout << "WebSocket server running on ws://localhost:10034" << std::endl;

   std::thread(broadcast_image, std::ref(screen)).detach();

   if (verbose) {
       std::thread(&ScreenShot::report_performance, &screen).detach();
   }

   try {
       ws_server.run();
   } catch (const websocketpp::exception& e) {
       std::cerr << "WebSocket server error: " << e.what() << std::endl;
   } catch (const std::exception& e) {
       std::cerr << "Server error: " << e.what() << std::endl;
   }

   XCloseDisplay(display);

   return 0;
}

void set_thread_priority(std::thread& thread, int priority) {
   sched_param sch_params;
   sch_params.sched_priority = priority;
   if (pthread_setschedparam(thread.native_handle(), SCHED_FIFO, &sch_params)) {
       std::cerr << "Failed to set thread priority: " << std::strerror(errno) << std::endl;
   }
}
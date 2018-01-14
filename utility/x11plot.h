//
// x11plot.h
//
// Plot on X11
//
// Copyright 2016 - 2018 Peter Andrews @ CSHL
//

#ifndef PAA_X11PLOT_H
#define PAA_X11PLOT_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <list>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "error.h"
#include "files.h"
#include "plot.h"
#include "strings.h"
#include "threads.h"
#include "utility.h"

namespace paa {

// Callback function defs
using void_fun = std::function<void ()>;
using bool_fun = std::function<bool ()>;
using void_uint_fun = std::function<void (const unsigned int)>;

// Point class
template <class Type>
struct PointT {
  constexpr PointT() = default;
  constexpr PointT(const Type x_, const Type y_) : x{x_}, y{y_} { }
  template <class Event>
  PointT(const Event & event) {  // NOLINT
    x = event.x;
    y = event.y;
  }

  template <class Event>
  PointT & operator=(const Event & event) {
    x = event.x;
    y = event.y;
    return *this;
  }
  Type operator[](const bool y_) const { return y_ ? y : x; }
  Type & operator[](const bool y_) { return y_ ? y : x; }
  bool operator==(const PointT rhs) const { return x == rhs.x && y == rhs.y; }
  bool operator!=(const PointT rhs) const { return x != rhs.x || y != rhs.y; }
  double distance(const PointT other) const {
    return sqrt((x - other.x) * (x - other.x) + (y - other.y) * (y - other.y));
  }
  double distance(const Type x_, const Type y_) const {
    return distance(PointT{x_, y_});
  }

  Type x{0};
  Type y{0};
};
using Point = PointT<int>;  // Integer point
using uPoint = PointT<unsigned int>;  // Unsigned integer point
using bPoint = PointT<bool>;  // Boolean point
using dPoint = PointT<double>;  // Double point

class X11Font {
 public:
  X11Font(Display * display_, const unsigned int point_size,
          const std::string font_name = "helvetica",
          const std::string font_weight = "bold",
          const unsigned int x_ppi = 100,
          const unsigned int y_ppi = 100,
          const bool fallback = false) : display{display_} {
    if (0) std::cerr << font_name << std::endl;
    const std::vector<std::string> font_name_trials{"*sans*", "utopia", "*"};
    for (const std::string & trial_name : font_name_trials) {
      const std::string font_spec{std::string("-*-") + trial_name +
            "-" + font_weight + "-r-normal-*-*-" +
            std::to_string(point_size) + "-" +
            std::to_string(x_ppi) + "-" + std::to_string(y_ppi) +
            "-p-0-iso8859-1"};
      font = XLoadQueryFont(display, font_spec.c_str());
      if (font) break;
    }
    if (fallback && !font) font = XLoadQueryFont(display, "fixed");
  }

  X11Font(X11Font &) = delete;
  X11Font & operator=(const X11Font &) = delete;
  X11Font & operator=(X11Font &&) = delete;
  X11Font(X11Font && other) : display{other.display}, font{other.font} {
    other.display = nullptr;
    other.font = nullptr;
  }

  operator bool() const { return font != nullptr; }
  Font id() const { return font->fid; }
  int width() const {
    return font->max_bounds.rbearing - font->max_bounds.lbearing;
  }
  int height() const {
    return font->max_bounds.ascent + font->max_bounds.descent;
  }
  int origin_height() const { return -font->max_bounds.descent; }
  int string_width(const std::string & text) const {
    // Fix this?
    return XTextWidth(font, text.c_str(),
                      static_cast<unsigned int>(text.size()));
  }
  int centered_y(const int y) const {
    return y + (font->max_bounds.ascent - font->max_bounds.descent) / 2;
  }
  int below_y(const int y) const {
    return y + font->max_bounds.ascent;
  }
  int centered_x(const std::string & text, const int x) const {
    return x - (string_width(text) + 1) / 2 -
        font->per_char[static_cast<int>(text[0])].lbearing + 1;
  }
  double d_centered_x(const std::string & text, const double x) const {
    return x - (string_width(text) + 1) / 2.0 -
        font->per_char[static_cast<int>(text[0])].lbearing + 1;
  }

  ~X11Font() {
    if (true && font) {
      XFreeFont(display, font);
    }
  }

  Display * display{nullptr};
  XFontStruct * font{nullptr};
};

class X11Fonts {
 public:
  static constexpr unsigned int max_font_size{500};

  template <class App>
  explicit X11Fonts(const App & app, const std::string & name = "helvetica") {
    Display * display{app.display};
    fonts.reserve(max_font_size);
    std::vector<uint64_t> indexes;
    std::vector<unsigned int> widths;
    std::vector<X11Font> temp_fonts;
    std::vector<unsigned int> temp_sizes;
    for (unsigned int tenth_points{40}; tenth_points <= max_font_size;
         tenth_points += 10) {
      X11Font font{display, tenth_points, name, "bold",
            app.pixels_per_inch(0), app.pixels_per_inch(1),
            tenth_points == max_font_size && fonts.empty()};
      if (font) {
        indexes.push_back(temp_fonts.size());
        widths.push_back(font.string_width("A test string to measure width"));
        temp_fonts.push_back(std::move(font));
        temp_sizes.push_back(tenth_points);
      }
    }
    sort(indexes.begin(), indexes.end(),
         [&widths](const unsigned int lhs, const unsigned int rhs) {
           return widths[lhs] < widths[rhs];
         });
    for (const uint64_t fi : indexes) {
      fonts.push_back(std::move(temp_fonts[fi]));
      lookup[temp_sizes[fi]] = static_cast<unsigned int>(sizes.size());
      sizes.push_back(temp_sizes[fi]);
    }
    if (fonts.empty()) throw Error("No fonts loaded");
  }

  X11Font * size(const unsigned int points) const {
    return &fonts[lookup.at(points)];  // can throw exception
  }
  X11Font * at_least(const unsigned int points) const {
    auto found = lower_bound(sizes.begin(), sizes.end(), points);
    if (found == sizes.end()) --found;
    return &fonts[found - sizes.begin()];
  }
  X11Font * at_most(const unsigned int points) const {
    auto found = upper_bound(sizes.begin(), sizes.end(), points);
    if (found == sizes.begin()) ++found;
    return &fonts[found - 1 - sizes.begin()];
  }
  X11Font * fits(const std::string text,
                 const int width, const int height) const {
    for (unsigned int f{0}; f != fonts.size(); ++f) {
      X11Font * font{&fonts[fonts.size() - f - 1]};
      if (font->height() > height) continue;
      if (font->string_width(text) < width) return font;
    }
    return &fonts[0];
  }
  void clear() { fonts.clear(); }

  std::map<unsigned int, unsigned int> lookup{};
  std::vector<unsigned int> sizes{};
  mutable std::vector<X11Font> fonts{};
};

using iBounds = std::vector<std::vector<int>>;
inline bool operator!=(const iBounds & lhs, const iBounds & rhs) {
  if (lhs.size() != rhs.size()) return true;
  for (unsigned int y{0}; y != lhs.size(); ++y) {
    if (lhs[y].size() != rhs[y].size()) return true;
    for (unsigned int d{0}; d != lhs[y].size(); ++d) {
      if (lhs[y][d] != rhs[y][d]) return true;
    }
  }
  return false;
}

std::string hex(const XColor & color) {
  std::string result;
  static std::string chars{"0123456789abcdefxxx"};
  for (const unsigned int component : {color.red, color.green, color.blue}) {
    result += chars[((component / 256) % 256) / 16];
    result += chars[(component % 256) / 16];
  }
  return result;
}

// Due to X11 Macros using hidden casts
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"

// X11 convenience functions
void draw_centered_oval(Display * display, Window window, GC gc_,
                        const int x, const int y,
                        const int x_rad, const int y_rad) {
  XDrawArc(display, window, gc_, x - x_rad, y - y_rad,
           2 * x_rad + 1, 2 * y_rad + 1, 0, 64 * 360);
}
void fill_centered_oval(Display * display, Window window, GC gc_,
                        const int x, const int y,
                        const int x_rad, const int y_rad) {
  XFillArc(display, window, gc_, x - x_rad, y - y_rad,
           2 * x_rad + 1, 2 * y_rad + 1, 0, 64 * 360);
}
void draw_centered_rectangle(Display * display, Window window, GC gc_,
                             const int x, const int y,
                             const int x_rad, const int y_rad) {
  XDrawRectangle(display, window, gc_, x - x_rad, y - y_rad,
                 2 * x_rad + 1, 2 * y_rad + 1);
}
void fill_centered_rectangle(Display * display, Window window, GC gc_,
                             const int x, const int y,
                             const int x_rad, const int y_rad) {
  XFillRectangle(display, window, gc_, x - x_rad, y - y_rad,
                 2 * x_rad + 2, 2 * y_rad + 2);
}
XRectangle rect(const unsigned int x, const unsigned int y,
                const unsigned int width_, const unsigned int height_) {
  XRectangle rect_;
  rect_.x = x;
  rect_.y = y;
  rect_.width = width_;
  rect_.height = height_;
  return rect_;
}
XRectangle rect(const iBounds & bounds) {
  return rect(bounds[0][0], bounds[1][0], bounds[0][2], bounds[1][2]);
}
bool operator!=(const XRectangle lhs, const XRectangle & rhs) {
  return lhs.x != rhs.x || lhs.y != rhs.y ||
      lhs.width != rhs.width || lhs.height != rhs.height;
}
bool operator!=(const XPoint lhs, const XPoint & rhs) {
  return lhs.x != rhs.x || lhs.y != rhs.y;
}

// Window class in an app
template <class X11App>
class X11WindowT {
 public:
  using X11Win = X11WindowT<X11App>;
  static constexpr unsigned int default_window_width{default_doc_width};
  static constexpr unsigned int default_window_height{default_doc_height};
  static constexpr double window_scale{1.65};
  static constexpr int radio_width{1};

  // Factory function
  static X11WindowT & create(X11App & app) {
    return app.add(std::make_unique<X11Win>(app));
  }

  X11WindowT(X11App & app_,
             const unsigned int width__ = default_window_width * window_scale,
             const unsigned int height__ = default_window_height * window_scale,
             const int x__ = 0, const int y__ = 0, const bool map_ = true,
             const std::string title = "") :
      app{app_}, size_{width__, height__},
    window{XCreateSimpleWindow(display(), DefaultRootWindow(display()),
                               x__, y__, width(), height(),
                               0, app.white, app.white)}
  {
    // Window properties
    XStoreName(display(), window, title.c_str());
    XSelectInput(display(), window, StructureNotifyMask | ExposureMask);
    XSetWMProtocols(display(), window, app.wmDeleteMessage(), 1);
    XSetWindowBackgroundPixmap(display(), window, None);
    if (true) {
      if (0) std::cerr << "Resizing to "
                       << width() << "x" << height() << " "
                       << x__ << " " << y__ << std::endl;
      XSizeHints hints;
      hints.flags = PPosition | PSize;
      hints.x = x__;
      hints.y = y__;
      hints.width = width();
      hints.height = height();
      XSetNormalHints(display(), window, &hints);
    }

    const Font font{app.fonts.at_most(300)->id()};

    // Colors
    XSync(display(), False);

    // Graphics contexts
    gc = create_gc(app.black, app.white);
    XSetFont(display(), gc, font);
    fill_gc = create_gc(app.white, app.black);
    XSetFont(display(), fill_gc, font);
    radio_gc = create_gc(app.black, app.white, radio_width,
                         LineSolid, CapButt, JoinMiter);

    // Maximum request size for draw events (for points; divide for arcs, etc)
    max_request = XMaxRequestSize(display()) - 3;

    if (map_) {
      XMapWindow(display(), window);
    }
  }

  X11WindowT(const X11WindowT &) = delete;
  X11WindowT & operator=(const X11WindowT &) = delete;

  virtual ~X11WindowT() {
    for (GC g : {gc, fill_gc, radio_gc}) XFreeGC(display(), g);
    if (pixmap_used) XFreePixmap(display(), pixmap);
    XDestroyWindow(display(), window);
  }

  // Window information
  Display * display() const { return app.display; }
  int width() const { return size_[0]; }
  int height() const { return size_[1]; }
  int extent(const bool y) { return size_[y]; }
  virtual bool slow() const { return false; }

  void set_window_offset() {
    int xo{0};
    int yo{0};
    XWindowAttributes xwa;
    XGetWindowAttributes(display(), window, &xwa);
    // std::cerr << "xwa " << xwa.x << " " << xwa.y << std::endl;
    Window child;
    XTranslateCoordinates(display(), window, DefaultRootWindow(display()),
                          0, 0, &xo, &yo, &child);
    // std::cerr << "trans " << xo << " " << yo << std::endl;
    window_offset[0] = xo - xwa.x;
    window_offset[1] = yo - xwa.y;
  }

  GC create_gc(const uint64_t foreground, const uint64_t background,
               const unsigned int line_width = 1,
               const unsigned int line_type = LineSolid,
               const unsigned int butt_type = CapButt,
               const unsigned int join_type = JoinMiter) const {
    const GC result{XCreateGC(display(), window, 0, nullptr)};
    XSetForeground(display(), result, foreground);
    XSetBackground(display(), result, background);
    XSetLineAttributes(display(), result, line_width,
                       line_type, butt_type, join_type);
    return result;
  }

  // Event actions
  virtual void mapped(const XMapEvent &) {
    // std::cerr << "mapped" << std::endl;
    set_window_offset();
  }
  virtual void configure(const XConfigureEvent & event) {
    if (size_[0] != static_cast<unsigned int>(event.width) ||
        size_[1] != static_cast<unsigned int>(event.height)) {
      just_configured = true;
      size_[0] = event.width;
      size_[1] = event.height;
      set_window_offset();
      prepare_draw();
    }
  }
  virtual void expose(const XExposeEvent & event) {
    if (event.count == 0) {
      set_window_offset();
      return prepare_draw();
    }
  }
  virtual void enter(const XCrossingEvent &) { }
  virtual void key(const XKeyEvent &) { }
  virtual void button_press(const XButtonEvent &) { }
  virtual void motion(const XMotionEvent &) { }
  virtual void button_release(const XButtonEvent &) { }
  virtual void leave(const XCrossingEvent &) { }
  virtual void client_message(const XClientMessageEvent &) { }

  void set_bounds(const bool y, const int l, const int h) {
    bounds[y][2] = (bounds[y][1] = h) - (bounds[y][0] = l);
  }
  void set_bounds(const int xl, const int xh, const int yl, const int yh) {
    const bool valid_initial_bounds{bounds.size() > 0};
    if (!valid_initial_bounds) bounds.assign(2, std::vector<int>(3));
    const iBounds last_bounds = bounds;
    set_bounds(0, xl, xh);
    set_bounds(1, yl, yh);
    if (bounds != last_bounds) {
      if (valid_initial_bounds) XFreePixmap(display(), pixmap);
      pixmap = XCreatePixmap(display(), window, width(), height(), app.depth);
      pixmap_used = true;
    }
  }
  template <class POINT>
  bool in_bounds(const POINT & point) const {
    return in_bounds(point.x, point.y);
  }
  bool in_bounds(const int x, const int y) const {
    return x > bounds[0][0] && x < bounds[0][1] &&
        y > bounds[1][0] && y < bounds[1][1];
  }

  virtual void save_image(const std::string & base_name,
                          void_fun call_back = [] () {}) {
    const std::string image_name{get_next_file(base_name, "xpm")};
    const std::string png_name{replace_substring(image_name, "xpm", "png")};
    save_image(image_name, window, 0, 0, width(), height(), call_back);
    image_names.push_back(image_name);
    std::ostringstream png_command;
    png_command << "convert " << image_names.back() << " "
                << png_name;
    if (system(png_command.str().c_str()) == -1) {
      std::cerr << "Problem creating png image" << std::endl;
    } else {
      std::cerr << "Converted image to " << png_name << std::endl;
    }
  }
  void save_image(const std::string & file_name, Drawable d,
                  const int xp, const int yp,
                  const unsigned int w, const unsigned int h,
                  void_fun call_back = [] () {}) {
    if (0) std::cerr << xp << " " << yp << " "
                     << w << " " << h << " " << std::endl;
    XImage * image{XGetImage(display(), d, xp, yp, w, h, AllPlanes, XYPixmap)};
    if (!image) throw Error("Could not get image");
    call_back();
    std::map<uint64_t, char> colors;
    XColor color;
    std::ostringstream color_string;
    std::ostringstream image_string;
    for (unsigned int y{0}; y != h; ++y) {
      image_string << '"';
      for (unsigned int x{0}; x != w; ++x) {
        color.pixel = XGetPixel(image, x, y);
        auto inserted = colors.emplace(color.pixel, 'a' + colors.size());
        if (inserted.second == true) {
          XQueryColor(display(), app.colormap, &color);
          color_string << '"' << inserted.first->second << " "
                       << "c #" << hex(color) << '"' << ",\n";
        }
        image_string << inserted.first->second;
      }
      image_string << '"' << (y + 1 == h ? "" : ",") << "\n";
    }
    std::ofstream file{file_name.c_str()};
    if (!file) throw Error("Problem opening file") << file_name;
    file << "/* XPM */\n"
         << "static char * XFACE[] = {\n"
         << "/* <Values> */\n"
         << "/* <width/cols> <height/rows> <colors> <char on pixel>*/\n"
         << '"' << w << " " << h << " " << colors.size()
         << " 1" << '"' << ",\n"
         << "/* <Colors> */\n"
         << color_string.str()
         << "/* <Pixels> */\n"
         << image_string.str()
         << "};\n";
    std::cerr << "Saved image to " << file_name << std::endl;
    XDestroyImage(image);
  }

  // Data preparation and drawing functions (just an empty window here)
  virtual void prepare() { }
  virtual void draw() {
    XFillRectangle(display(), window, fill_gc, 0, 0, width(), height());
  }
  void prepare_draw() { prepare(); draw(); }

  X11App & app;
  // unsigned int size_[2]{0, 0};
  uPoint size_{};
  Point window_offset{};
  iBounds bounds{};
  Window window{};
  Pixmap pixmap{};
  bool pixmap_used{false};
  std::vector<std::string> image_names{};
  bool inside{true};

  GC gc{}, fill_gc{}, radio_gc{};
  uint64_t max_request{};
  bool destroyed{false};
  mutable bool just_configured{true};
};

inline Display * open_default_display() {
  Display * display = XOpenDisplay(nullptr);
  if (display == nullptr) {
    throw Error("Could not open X display - "
                "is X windowing enabled in your terminal?");
  }
  return display;
}

// Application to display several windows of different types
class X11App {
 public:
  using X11Win = X11WindowT<X11App>;
  using WinPtr = std::unique_ptr<X11Win>;
  using WinList = std::list<WinPtr>;
  using WinIter = WinList::iterator;

  X11App() : display{open_default_display()},
    screen{DefaultScreen(display)}, depth{DefaultDepth(display, screen)},
    colormap{DefaultColormap(display, screen)},
    display_size{static_cast<unsigned int>(DisplayWidth(display, screen)),
          static_cast<unsigned int>(DisplayHeight(display, screen))},
    display_mm{static_cast<unsigned int>(DisplayWidthMM(display, screen)),
          static_cast<unsigned int>(DisplayHeightMM(display, screen))},
    fonts{*this},
    black{BlackPixel(display, screen)},
    white{WhitePixel(display, screen)} {
      if (!display) throw Error("Problem opening X11 display");
      wmDeleteMessage_ = {XInternAtom(display, "WM_DELETE_WINDOW", False)};
    }

  X11App(const X11App &) = delete;
  X11App & operator=(const X11App &) = delete;

  ~X11App() {
    fonts.clear();
    windows.clear();
    XFreeColormap(display, colormap);
    XCloseDisplay(display);
  }

  // Add a window to app
  X11Win & add(WinPtr && ptr) {
    WinIter win = windows.emplace(windows.end(), std::move(ptr));
    window_lookups[(*win)->window] = win;
    return **win;
  }

  unsigned int pixels_per_inch(const bool y) const {
    return 25.4 * display_size[y] / display_mm[y];
  }
  unsigned int pixels_per_inch() const {
    return std::max(pixels_per_inch(0), pixels_per_inch(1));
  }

  // Run the application
  void run() {
    // Only keep latest configure requests to avoid too many redraws
    std::map<Window, XConfigureEvent> configures;

    // Event loop
    while (windows.size()) {
      // Configure only when no events are pending
      if (!XPending(display)) {
        for (auto pair : configures) {
          (*window_lookups[pair.first])->configure(pair.second);
        }
        configures.clear();
      }

      // Get event
      XNextEvent(display, &event);

      // Process event based on type
      switch (event.type) {
        case ConfigureNotify:
          {
            const Window win{event.xconfigure.window};
            WinIter win_iter{window_lookups[win]};
            if ((*win_iter)->slow()) {
              configures[win] = event.xconfigure;
            } else {
              (*win_iter)->configure(event.xconfigure);
            }
          }
          break;

        case MapNotify:
          (*window_lookups[event.xmap.window])->mapped(event.xmap);
          break;

        case VisibilityNotify:
          break;

        case Expose:
          (*window_lookups[event.xcrossing.window])->expose(event.xexpose);
          break;

        case EnterNotify:
          (*window_lookups[event.xcrossing.window])->enter(event.xcrossing);
          break;

        case KeyPress:
          (*window_lookups[event.xkey.window])->key(event.xkey);
          break;

        case ButtonPress:
          (*window_lookups[event.xbutton.window])->button_press(event.xbutton);
          break;

        case MotionNotify:
          (*window_lookups[event.xmotion.window])->motion(event.xmotion);
          break;

        case ButtonRelease:
          (*window_lookups[event.xbutton.window])->
              button_release(event.xbutton);
          break;

        case LeaveNotify:
          (*window_lookups[event.xcrossing.window])->leave(event.xcrossing);
          break;

        case ClientMessage:
          if (static_cast<uint64_t>(event.xclient.data.l[0]) ==
              *wmDeleteMessage()) {
            const Window window{event.xclient.window};
            close_window(window);
          } else {
            (*window_lookups[event.xmotion.window])->
                client_message(event.xclient);
          }
          break;

        case DestroyNotify:
          break;

        default:
          break;
      }
    }
  }

  void close_window(const Window window) {
    XSelectInput(display, window, 0);
    while (XCheckWindowEvent(display, window, -1UL, &event)) { }
    windows.erase(window_lookups[window]);
    window_lookups.erase(window);
  }

  Atom * wmDeleteMessage() const {  // Allows proper pass of window kill
    return const_cast<Atom *>(&wmDeleteMessage_);
  }

  bool exists(const Window & win) const {
    return window_lookups.count(win);
  }

  Display * display{};
  int screen{};
  int depth{};
  Colormap colormap{};
  // unsigned int display_size[2]{0, 0};
  // unsigned int display_mm[2]{0, 0};
  uPoint display_size{};
  uPoint display_mm{};
  X11Fonts fonts;
  uint64_t black{}, white{};
  XEvent event{};

  Atom wmDeleteMessage_{};
  WinList windows{};

 private:
  std::map<Window, WinIter> window_lookups{};
  Point last_position{-1, -1};
};
using X11Win = X11App::X11Win;

struct Actions {
  Actions(void_fun press_ = []() {}, bool_fun visible_ = []() { return true; },
          void_fun release_ = []() {}) :
      press{press_}, visible{visible_}, release{release_} { }

  void_fun press;  // Function to take action on press
  bool_fun visible;  // Function to decide whether to show radio
  void_fun release;  // Function to take action on release
};

class Event {
 public:
  enum EventType {
    Prepare,
    PreDraw,
    Draw,
    X
  };
  explicit Event(const EventType type_ = Draw,
                 const XEvent * x_ = nullptr) :
      type{type_}, x{x_} {}

  EventType type{Draw};
  const XEvent * x{nullptr};
};

// Radio button widget
struct Radio {
  // Constructor
  Radio(const std::string & description_, X11Win * win_,
        const dPoint specification_, const Actions & actions_ = Actions(),
        const bool togglable_ = false, const bool start_toggle = false,
        const GC * gc_ = nullptr) :
      description{description_}, win{win_},
    specification{specification_},
    actions{actions_}, togglable{togglable_}, toggled{start_toggle},
    gc{gc_ == nullptr ? win->radio_gc : *gc_} { }

  // These are not strictly needed - just to avoid warning due to GC pointer
  Radio(const Radio &) = default;
  Radio & operator=(const Radio &) = default;

  // State testing and assignment
  operator bool() const { return toggled; }
  Radio & operator=(const bool state) {
    toggled = state;
    draw();
    return *this;
  }

  // Location of corners of graph region
  Point corner(const bool high_x, const bool high_y) const {
    return Point{high_x ? win->bounds[0][1] : win->bounds[0][0],
          high_y ? win->bounds[1][1] : win->bounds[1][0]};
  }

  int min_border() const {
    return min(win->bounds[0][0], win->bounds[1][0],
               win->width() - win->bounds[0][1],
               win->height() - win->bounds[1][1]);
  }

  // Location in window of radio button
  Point location() const {
    const bPoint high{specification.x < 0, specification.y < 0};
    const Point anchor{corner(high[0], high[1])};
    Point point;
    // const int border{min_border()};
    const double border{1.0 * min_border()};
    for (const bool y : {false, true}) {
      // Specification near but not equal to zero is at edges
      if (fabs(specification[y]) > 0 && fabs(specification[y]) < 50) {
        point[y] = anchor[y] + border *
            (specification[y] + 0.5 + (high[y] ? 1 : -2));
      } else {
        // Specification of zero or around 100 is centered
        const double centered{specification[y] -
              (fabs(specification[y]) > 0 ? 100 : 0)};
        point[y] = (win->bounds[y][0] + win->bounds[y][1]) / 2 +
            centered * border;
      }
    }
    return point;
  }

  double radius() const { return radius_scale * min_border() / 3.0; }
  bool contains(const Point point) const {
    return location().distance(point) < radius();
  }
  bool visible() const { return actions.visible(); }

  // What to do when radio is pressed
  template <class Point>
  bool press(const Point point) {
    if (!actions.visible()) {
      skip_release = true;
      return contains(point);
    } else if (contains(point)) {
      toggled = !toggled;
      draw();
      actions.press();
      return true;
    }
    return false;
  }

  // What to do when radio is released
  template <class Point>
  bool release(const Point point) {
    // if (skip_release) return skip_release = false;
    if (skip_release) {
      skip_release = false;
      return contains(point);
    }
    if (contains(point)) {
      if (!togglable) toggled = !toggled;
      draw();
      actions.release();
      return true;
    }
    return false;
  }

  void erase() const {
    const Point point{location()};
    fill_centered_oval(win->display(), win->window, win->fill_gc,
                       point.x, point.y, radius() + 1, radius() + 1);
  }

  void draw() const {
    const Point point{location()};
    static GC grey_gc{[this]() {
        XColor grey;
        if (!XAllocNamedColor(win->display(), win->app.colormap, "rgb:dd/dd/dd",
                              &grey, &grey)) throw Error("Could not get grey");
        return win->create_gc(grey.pixel, win->app.white);
      }()};
    if (win->inside) {
      fill_centered_oval(win->display(), win->window, win->fill_gc,
                         point.x, point.y, radius() + 1, radius() + 1);
      // if (!actions.visible()) return;
      draw_centered_oval(win->display(), win->window,
                         actions.visible() ? gc : grey_gc,
                         point.x, point.y, radius(), radius());
      if (toggled) {
        fill_centered_oval(win->display(), win->window, gc,
                           point.x, point.y, radius() / 2, radius() / 2);
      } else {
        fill_centered_oval(win->display(), win->window, win->fill_gc,
                           point.x, point.y, radius() / 2, radius() / 2);
      }
    } else {
      erase();
    }
  }

  // Data
  std::string description;  // Help text for radio
  X11Win * win;  // The window attached to
  dPoint specification;  // Where on page
  Actions actions;  // Actions to perform
  bool togglable;  // Can radio be toggled
  bool toggled;  // State of radio
  GC gc;  // Color for radio, line width, etc
  bool skip_release{false};
  double radius_scale{1.0};
  unsigned int id{0};
};

class Click : public Point {
 public:
  class Resetter {
   public:
    explicit Resetter(Click & click_) : click{click_} { }
    ~Resetter() { click.reset(); }

   private:
    Click & click;
  };

  Click() : Point{} {}

  explicit Click(const XButtonEvent & event) {
    *this = event;
  }

  Click & operator=(const XButtonEvent & event) {
    this->Point::operator=(event);
    if (event.button == Button2 || event.state & ShiftMask) {
      value = 2;
    } else if (event.button == Button3 || event.state & ControlMask) {
      value = 3;
    } else if (event.button == Button1) {
      value = 1;
    } else {
      // Buttons 4, 5 act like no button was pressed
      value = 0;
    }
    return *this;
  }

  bool operator==(const unsigned int mouse_button) const {
    return value == mouse_button;
  }
  bool operator!=(const unsigned int mouse_button) const {
    return value != mouse_button;
  }
  bool operator<(const unsigned int mouse_button) const {
    return value < mouse_button;
  }
  bool operator<=(const unsigned int mouse_button) const {
    return value <= mouse_button;
  }
  bool operator>(const unsigned int mouse_button) const {
    return value > mouse_button;
  }
  bool operator>=(const unsigned int mouse_button) const {
    return value > mouse_button;
  }

  // operator unsigned int() const { return value; }
  void reset() { value = 0; }

 private:
  unsigned int value{0};
};

class Color {
 public:
  explicit Color(std::string color_name) {
    replace_substring(color_name, "rgb:", "");
    std::istringstream name{color_name.c_str()};
    std::string hex;
    getline(name, hex, '/');
    r = strtol(hex.c_str(), nullptr, 16);
    getline(name, hex, '/');
    g = strtol(hex.c_str(), nullptr, 16);
    getline(name, hex, '/');
    b = strtol(hex.c_str(), nullptr, 16);
    if (0) std::cerr << "Converted " << color_name << " to "
                     << r << " " << g << " " << b << " "
                     << to_string() << std::endl;
  }

  Color(const unsigned int r_, const unsigned int g_, const unsigned int b_) :
      r{r_}, g{g_}, b{b_} { }

  // Find color of maximum distance to others
  explicit Color(const std::vector<Color> & colors,
                 const unsigned int step = 8,
                 const unsigned int min_white_distance2 = 2048,
                 const unsigned int min_black_distance2 = 1024) {
    if (colors.empty()) throw Error("Empty color list");
    const Color white{255, 255, 255};
    const Color black{0, 0, 0};
    Color best{colors.front()};
    int64_t best_distance2{0};
    for (r = 0; r < 256; r += step) {
      for (g = 0; g < 256; g += step) {
        for (b = 0; b < 256; b += step) {
          if (distance2(white) < min_white_distance2) continue;
          if (distance2(black) < min_black_distance2) continue;
          int64_t min_distance2{std::numeric_limits<int64_t>::max()};
          for (const Color & existing : colors) {
            const int64_t trial_distance2{distance2(existing)};
            if (min_distance2 > trial_distance2) {
              min_distance2 = trial_distance2;
            }
          }
          if (best_distance2 < min_distance2) {
            best_distance2 = min_distance2;
            best = *this;
          }
        }
      }
    }
    *this = best;
  }

  int64_t distance2(const Color & other) {
    // www.compuphase.com/cmetric.htm
    const int64_t ar{(r + other.r) / 2};
    const int64_t rd{r - other.r};
    const int64_t gd{g - other.g};
    const int64_t bd{b - other.b};
    return (((512 + ar) * rd * rd) >> 8) + 4 * gd * gd +
        (((767 - ar) * bd * bd) >> 8);
  }

  std::string to_string() const {
    std::ostringstream result{};
    result << "rgb:";
    for (unsigned int c{0}; c != 3; ++c) {
      if (c) result << "/";
      result.width(2);
      result.fill('0');
      result << std::hex << std::internal;
      result << (&r)[c];
    }
    return result.str();
  }

 private:
  int64_t r{0};
  int64_t g{0};
  int64_t b{0};
};

class X11Colors : public X11Win {
 public:
  using CallBack = void_uint_fun;

  X11Colors(const X11Colors &) = delete;
  X11Colors & operator=(const X11Colors &) = delete;

  static const int side{600};
  static X11Colors & create(X11App & app,
                            const std::vector<std::string> & starting_colors,
                            const size_t n_colors_ = 0,
                            const bool order = false,
                            const unsigned int width_ = side,
                            const unsigned int height_ = side,
                            const int x_off_ = 0,
                            const int y_off_ = 0,
                            const CallBack call_back_ =
                            [] (const unsigned int) { },
                            const bool close_on_click_ = false,
                            const std::string title = "") {
    return reinterpret_cast<X11Colors &>(
        app.add(std::make_unique<X11Colors>(
            app, starting_colors,
            n_colors_ ? n_colors_ : starting_colors.size(), order,
            width_, height_, x_off_, y_off_,
            call_back_, close_on_click_, title)));
  }

  explicit X11Colors(X11App & app__,
                     const std::vector<std::string> & starting_colors,
                     const size_t n_colors_ = 0,
                     const bool order = false,
                     const unsigned int width_ = side,
                     const unsigned int height_ = side,
                     const int x_off_ = 0,
                     const int y_off_ = 0,
                     const CallBack call_back_ = [] (const unsigned int) { },
                     const bool close_on_click_ = false,
                     const std::string title = "") :
      X11Win{app__, width_, height_,
        static_cast<int>(x_off_ + (order ? width_ + width_ / 20: 0)), y_off_,
        true, title},
    color_names{starting_colors},
    n_colors{n_colors_ ? n_colors_ : starting_colors.size()},
    n_x{static_cast<unsigned int>(ceil(sqrt(n_colors)))},
    n_y{static_cast<unsigned int>(ceil(1.0 * n_colors / n_x))},
    call_back{call_back_},
    close_on_click{close_on_click_} {
      if (false) std::cerr << n_colors << " " << n_x * n_y << " "
                           << n_x << " " << n_y << std::endl;
      XSelectInput(display(), window,
                   StructureNotifyMask | ExposureMask |
                   ButtonPressMask | ButtonReleaseMask);

      // Shrink initial color name list if too long
      if (color_names.size() > n_colors) {
        color_names.resize(n_colors);
      }

      // Make initial colors
      for (const std::string & color_name : color_names) {
        colors.emplace_back(color_name);
      }

      // Expand initial color list if necessary
      const bool progress{false};
      const size_t initial_size{color_names.size()};
      if (color_names.size() != n_colors) {
        if (progress) std::cerr << "Size";
        while (color_names.size() != n_colors) {
          const unsigned int step{static_cast<unsigned int>(
              256 / pow(colors.size(), 1.0 / 3) / 2 + 1)};
          if (progress)
            std::cerr << " " << color_names.size() << " " << step << std::flush;
          colors.emplace_back(colors);
          color_names.push_back(colors.back().to_string());
        }
        if (progress) std::cerr << std::endl;
        // Move first made color to end
        if (color_names.size() != initial_size) {
          colors.push_back(colors[initial_size]);
          color_names.push_back(color_names[initial_size]);
          colors.erase(colors.begin() + initial_size);
          color_names.erase(color_names.begin() + initial_size);
        }
      }

      // Order colors, closest next to each other as a test
      if (order) {
        for (std::vector<Color>::iterator first{colors.begin()};
             first + 1 != colors.end(); ++first) {
          std::vector<Color>::iterator best = first;
          int64_t min_distance2{std::numeric_limits<int64_t>::max()};
          for (std::vector<Color>::iterator second{next(first)};
               second != colors.end(); ++second) {
            const int64_t dist2{first->distance2(*second)};
            if (dist2 < min_distance2) {
              min_distance2 = dist2;
              best = second;
            }
          }
          std::swap(*next(first), *best);
        }

        // Impose snake pattern on grid
        unsigned int n{0};
        for (std::vector<Color>::iterator first{colors.begin()};
             first < colors.end(); first += n_x) {
          if ((n++ % 2) == 0) continue;
          reverse(first, std::min(first + n_x, colors.end()));
        }

        color_names.clear();
        for (const Color & color : colors) {
          color_names.push_back(color.to_string());
        }
      }

      // X Colors and GCs
      Xcolors.resize(color_names.size());
      gcs.resize(color_names.size());
      for (unsigned int c{0}; c != color_names.size(); ++c) {
        std::string & color_name{color_names[c]};
        XColor & color{Xcolors[c]};
        if (!XAllocNamedColor(display(), app.colormap, color_name.c_str(),
                              &color, &color))
          throw Error("Could not get color") << color_name;

        // For colored arcs for points
        gcs[c] = create_gc(color.pixel, app.white, 2,
                           LineSolid, CapButt, JoinMiter);
      }

      // Cell borders
      border_x_gc = create_gc(app.white, app.black, x_border_width(),
                              LineSolid, CapButt, JoinMiter);
      border_y_gc = create_gc(app.white, app.black, y_border_height(),
                              LineSolid, CapButt, JoinMiter);
    }

  virtual void button_press(const XButtonEvent & event) {
    const Click click{event};
    if (click == 0) return;
    if (click > 1) close_on_click = true;

    // Determine which color was pressed
    const unsigned int x{n_x * event.x / width()};
    const unsigned int y{n_y * event.y / height()};
    const unsigned int i{x + n_x * y};
    call_back(i);
  }

  virtual void button_release(const XButtonEvent & event) {
    const Click click{event};
    if (click == 0) return;
    if (close_on_click) app.close_window(window);
  }

  void print_names() const {
    // Output color names
    for (unsigned int c{0}; c != color_names.size(); ++c) {
      if (c) {
        std::cout << ",";
        if ((c % 4) == 0) {
          std::cout << std::endl;
        } else {
          std::cout << " ";
        }
      }
      std::cout << '"' << color_names[c] << '"';
    }
    std::cout << std::endl;                          \
  }

  unsigned int x_border_width() const { return 1 + width() / n_x / 10; }
  unsigned int y_border_height() const { return 1 + height() / n_y / 10; }

  virtual void draw() {
    XFillRectangle(display(), window, fill_gc, 0, 0, width(), height());

    unsigned int c{0};
    const double box_width{1.0 * width() / n_x};
    const double box_height{1.0 * height() / n_y};
    for (unsigned int y{0}; y != n_y; ++y) {
      const unsigned int low_y{static_cast<unsigned int>(box_height * y)};
      for (unsigned int x{0}; x != n_x; ++x) {
        const unsigned int low_x{static_cast<unsigned int>(box_width * x)};
        if (c < color_names.size()) {
          XFillRectangle(display(), window, gcs[c],
                         low_x, low_y, box_width + 1, box_height + 1);
        }
        ++c;
      }
    }

    // Draw borders between cells
    XSetLineAttributes(display(), border_x_gc, x_border_width(),
                       LineSolid, CapButt, JoinMiter);
    XSetLineAttributes(display(), border_y_gc, y_border_height(),
                       LineSolid, CapButt, JoinMiter);
    for (unsigned int x{0}; x <= n_x; ++x) {
      const unsigned int low_x{static_cast<unsigned int>(box_width * x)};
      XDrawLine(display(), window, border_x_gc, low_x, 0, low_x, height());
    }
    for (unsigned int y{0}; y <= n_y; ++y) {
      const unsigned int low_y{static_cast<unsigned int>(box_height * y)};
      XDrawLine(display(), window, border_y_gc, 0, low_y, width(), low_y);
    }

    XFlush(display());
  }

  virtual ~X11Colors() {
    XFreeGC(display(), border_x_gc);
    XFreeGC(display(), border_y_gc);
    for (GC & gc_ : gcs) {
      XFreeGC(display(), gc_);
    }
  }

  std::vector<std::string> color_names{};

 private:
  std::vector<Color> colors{};
  std::vector<XColor> Xcolors{};
  std::vector<GC> gcs{};
  GC border_x_gc{};
  GC border_y_gc{};
  size_t n_colors;
  unsigned int n_x;
  unsigned int n_y;
  CallBack call_back;
  bool close_on_click;
};

using Range = std::vector<std::vector<double>>;

class SavedConfig {
 public:
  static constexpr double default_arc_radius{4};
  static constexpr double default_arc_width{2};
  static constexpr int default_line_width{4};
  static constexpr int default_line_type{LineSolid};

  SavedConfig() {}
  virtual ~SavedConfig() = default;
  bool operator!=(const SavedConfig & rhs) const {
    return dne(arc_radius, rhs.arc_radius) ||
        dne(arc_width, rhs.arc_width) ||
        line_width != rhs.line_width ||
        line_type != rhs.line_type ||
        series_order != rhs.series_order ||
        range != rhs.range ||
        max_range != rhs.max_range ||
        zoomed != rhs.zoomed ||
        drawn != rhs.drawn ||
        radio_states != rhs.radio_states;
  }
  void restore_config(const SavedConfig & rhs) {
    *this = rhs;
    return;
  }

  double arc_radius{default_arc_radius};
  double arc_width{default_arc_width};
  int line_width{default_line_width};
  int line_type{default_line_type};
  std::vector<unsigned int> series_order{};
  Range range{{unset(1.0), nunset(1.0), 0}, {unset(1.0), nunset(1.0), 0}};
  Range max_range{range};
  std::vector<unsigned char> zoomed{false, false};
  mutable bool drawn{false};
  std::vector<unsigned char> radio_states{};
};

class X11Graph : public X11Win, public SavedConfig {
 public:
  // Graph constants
  static constexpr unsigned int max_series{512};
  static constexpr int border_width{3};
  static constexpr unsigned int default_width{1280};
  static constexpr unsigned int default_height{720};

  // Graph data typedefs
  using Values = std::vector<double>;
  using XYSeries = std::vector<const Values *>;
  using Data = std::vector<XYSeries>;
  using CallBack = std::function<bool (X11Graph &, Event &)>;

  // Graph factories
  static X11Graph & create_whole(
      X11App & app, const Data & data__,
      const unsigned int width_ = default_width,
      const unsigned int height_ = default_height,
      const int x_off_ = 0, const int y_off_ = 0,
      const std::string title = "",
      const unsigned int n_threads__ = std::thread::hardware_concurrency());
  template <class ... Input>
  static X11Graph & create(X11App & app, Input && ... input);

  // Graph constructors and destructors and copying
  X11Graph(X11App & app__, const Data & data__,
           const unsigned int width_ = default_width,
           const unsigned int height_ = default_height,
           const int x_off_ = 0, const int y_off_ = 0,
           const std::string title = "",
           const unsigned int n_threads__ =
           std::thread::hardware_concurrency());
  template <class ... Input>
  X11Graph(X11App & app__, Input && ... input);
  virtual ~X11Graph();
  X11Graph(const X11Graph &) = delete;
  X11Graph & operator=(const X11Graph &) = delete;

  // Graph initialization functions
  template <class ... Input>
  void add_input(Values & x__, Values & y__, Input && ... input);
  void add_input() { }
  void add_call_back(const std::string & help_text,
                     const CallBack & call_back,
                     const bool full_draw = false,
                     const bool initially_on = true);
  void initialize();

  // Range functions
  void get_range(const unsigned int a = 2);
  void set_range(const bool y, const double low, const double high);
  void range_jump(const bool y, const double dist);
  bool in_range(const double x, const double y) const;
  bool in_range(const dPoint pos) const;
  void show_range(const std::string prefix) const;

  // Coordinates and transformations
  int coord(const bool y, const double val) const;
  double dcoord(const bool y, const double val) const;
  Point coord(const dPoint point) const;
  XPoint xcoord(const dPoint point) const;
  XPoint xcoord(const Point point) const;
  double icoord(const bool y, const int val) const;
  dPoint icoord(const Point point) const;
  unsigned int get_quadrant(const Point point) const;
  int min_border() const;

  // Event loop callbacks and related functions
  virtual void expose(const XExposeEvent & event);
  virtual void enter(const XCrossingEvent &);
  virtual void key(const XKeyEvent &);
  virtual void button_press(const XButtonEvent & event);
  virtual void motion(const XMotionEvent & event);
  virtual void button_release(const XButtonEvent & event);
  virtual void leave(const XCrossingEvent &);
  virtual void prepare();
  virtual void draw();

  // Prepare and draw helpers
  bool do_arcs(const unsigned int s) const;
  bool do_arcs() const;
  bool can_do_arcs() const;
  bool do_lines(const unsigned int s) const;
  bool do_lines() const;
  bool can_do_lines() const;
  void prepare_log();
  static std::string long_status(const bool in, const bool y);
  void draw_status(const bool force = false) const;
  void draw_controls();
  void draw_grid() const;
  void draw_ticks();
  void redraw();
  void erase_border();
  void set_clip_rectangle(const unsigned int x, const unsigned int y,
                          const unsigned int width_,
                          const unsigned int height_);
  void set_line_widths(std::vector<GC> gcs, const int width_);
  double line_vertical_y(const dPoint low_x, const dPoint high_x,
                         const double x) const;
  double line_horizontal_x(const dPoint low_x, const dPoint high_x,
                           const double y) const;
  XPoint line_bounds_intersection(const dPoint in, const dPoint out) const;

  // Assorted functions
  virtual bool slow() const;
  bool movie(const bool right);
  virtual void save_image(const std::string & base_name,
                          void_fun call_back = void_fun());

  // Data is series * (x, y) -> point
  Data input_data{}, log_data{}, log_x_data{}, log_y_data{};
  Data * data{&input_data};
  std::vector<std::unique_ptr<Values> > log_series{};

  // Graphics contexts and fonts
  GC border_gc{}, border_fill_gc{}, minor_gc{}, major_gc{}, tick_label_gc{};
  mutable X11Font * tick_font{nullptr};
  mutable X11Font * status_font{nullptr};

  // Colors and GCs and shapes and names for series
  std::vector<std::string> make_colors() const;
  void set_color(const unsigned int series, const unsigned int color);
  void reset_colors();
  bool colors_changed{false};
  std::vector<std::string> color_names{};
  std::vector<std::string> series_names{};
  std::vector<XColor> series_colors{};
  std::vector<GC> series_arc_gcs{};
  std::vector<GC> series_line_gcs{};
  std::vector<GC> series_radio_gcs{};
  std::vector<Radio> series_radios{};
  std::vector<uint8_t> series_only_arcs{}, series_only_lines{};
  std::vector<std::vector<XArc> > arcs{};
  std::vector<std::vector<XPoint> > points{};

  // Graph state information
  std::string status{""};
  std::vector<double> scale{};  // x, y, y / x
  Click click{};
  Point last_motion{};
  bool moved{false};
  bool small_move{false};

  //
  // Radio controls
  //
  Radio help_radio{"Toggle showing help text for controls", this, {1, 2},
    {[this]() { coord_radio = false; draw_controls(); }}, true, true};
  Radio coord_radio{"Toggle showing coordinates of cursor", this, {1, 3},
    {[this]() { help_radio = false; status = ""; draw_controls(); }}, true};
  Radio arcs_radio{"Draw a marker at each graph point", this, {-1, -2},
    {[this]() { return arcs_radio ? prepare_draw() : draw(); },
          [this]() { return can_do_arcs(); }}, true, true};
  Radio outlines_radio{"Toggle between solid and outlined markers", this,
    {-1, -5.5}, {[this]() { draw(); }, [this]() { return do_arcs(); }}, true};
  Radio lines_radio{"Connect graph points by lines", this, {-2, -1},
    {[this]() { return lines_radio ? prepare_draw() : draw(); },
          [this]() { return can_do_lines(); }},  true};
  Radio tick_radios[2]{
    {"Toggle axis labels on X axis (shown when cursor leaves window)",
          this, {5.5, -1},
      {[this]() { }}, true},
    {"Toggle axis labels on Y axis (shown when cursor leaves window)",
          this, {1, -5.5},
      {[this]() { }}, true}};
  Radio log_radios[2]{
    {"Toggle logarithmic scale on X axis", this, {6.5, -1},
      {[this]() { prepare_log(); prepare_draw(); }}, true},
    {"Toggle logarithmic scale on Y axis", this, {1, -6.5},
      {[this]() { prepare_log(); prepare_draw(); }}, true}};
  Radio grid_radios[2][2]{
    {{"Toggle major grid lines on X axis", this, {4.25, -1},
        {[this]() { if (grid_radios[0][0]) { return draw_grid(); }
            grid_radios[1][0] = false; redraw(); }}, true, true},
      {"Toggle major grid lines on Y axis", this, {1, -4.25},
        {[this]() { if (grid_radios[0][1]) { return draw_grid(); }
            grid_radios[1][1] = false; redraw(); }}, true, true}},
    {{"Toggle minor grid lines on X axis", this, {3.25, -1},
        {[this]() { if (grid_radios[1][0]) { grid_radios[0][0] = true;
              draw_grid(); } redraw(); }}, true, true},
      {"Toggle minor grid lines on Y axis", this, {1, -3.25},
        {[this]() { if (grid_radios[1][1]) { grid_radios[0][1] = true;
              draw_grid(); } redraw(); }}, true, true}}};
  Radio movie_radios[2]{
    {"Play a movie traveling left", this, {97.5, -1},
      {[this]() { movie(false); }, [this]() { return zoomed[0]; }}, true},
    {"Play a movie traveling right", this, {102.5, -1},
      {[this]() { movie(true); }, [this]() { return zoomed[0]; }}, true}};
  Radio restrict_range_radios[2]{  // unused
    {"Toggle range restriction on X axis to actual data range", this, {3, -1},
      {[this]() { get_range(0); prepare_draw(); }}, true, true},
    {"Toggle range restriction on Y axis to actual data range", this, {1, -3},
      {[this]() { get_range(1); prepare_draw(); }}, true, true}};
  Radio previous_views_radio{"Show previous view",
        this, {-1, 1}, {[this]() { },
          [this]() {return saved_config.size() > 1; },
          {[this]() {
              if (saved_config.size() > 1) {
                saved_config.pop_back();
                restore_config(saved_config.back());
                prepare_draw();
              }}}}};
  std::vector<Radio> unnamed_radios{};
  std::vector<Radio> extra_radios{};
  std::deque<Radio *> radios{&help_radio, &coord_radio,
        &arcs_radio, &outlines_radio, &lines_radio,
        &tick_radios[0], &tick_radios[1],
        &log_radios[0], &log_radios[1],
        &grid_radios[0][0], &grid_radios[0][1],
        &grid_radios[1][0], &grid_radios[1][1],
        &movie_radios[0], &movie_radios[1],
        &previous_views_radio};
  std::vector<Radio> create_unnamed_radios();
  bool_fun radio_tester(const Radio & radio, const bool state = true);
  bool_fun zoom_tester(const bool y);

  // Callback functions
  std::vector<CallBack> call_backs{};
  std::vector<Radio> call_back_radios{};

  // Saved configuration history
  std::deque<SavedConfig> saved_config{};
  std::vector<Radio *> saved_radios{
    &arcs_radio, &outlines_radio, &lines_radio};
  SavedConfig current_config() const;
  void restore_config(const SavedConfig & config);
  void save_config(const SavedConfig & config);

  // Number of threads to use
#ifdef __CYGWIN__
  unsigned int n_threads_{1};
#else
  unsigned int n_threads_{std::thread::hardware_concurrency()};
#endif
  ThreadPool pool{n_threads()};
  unsigned int n_threads() const;
  void n_threads(const unsigned int n_threads__);

  // Web url open
  void open_url(const std::string & url) const;
};

// Creation factory from data in exact format needed
X11Graph & X11Graph::create_whole(
    X11App & app, const Data & data__,
    const unsigned int width_, const unsigned int height_,
    const int x_off_, const int y_off_,
    const std::string title,
    const unsigned int n_threads__) {
  return reinterpret_cast<X11Graph &>(app.add(
      std::make_unique<X11Graph>(
    app, data__, width_, height_, x_off_, y_off_, title, n_threads__)));
}

// Creation factory from a bunch of vectors x1, y1, x2, y2, ...
template <class ... Input>
X11Graph & X11Graph::create(X11App & app, Input && ... input) {
  return reinterpret_cast<X11Graph &>(app.add(
      std::make_unique<X11Graph>(app, std::forward<Input>(input)...)));
}

// Construct from data in exact format needed
X11Graph::X11Graph(X11App & app__, const Data & data__,
                   const unsigned int width_,
                   const unsigned int height_,
                   const int x_off_, const int y_off_,
                   const std::string title,
                   const unsigned int n_threads__) :
    X11Win{app__, width_, height_, x_off_, y_off_, true, title},
      input_data{data__}, data{&input_data}, n_threads_{n_threads__} {
                            initialize();
                          }

// Construct from a bunch of vectors x1, y1, x2, y2, ...
template <class ... Input>
X11Graph::X11Graph(X11App & app__, Input && ... input) :
    X11Win{app__, default_width, default_height, 0, 0, true} {
  add_input(std::forward<Input>(input)...);
  data = &input_data;
  initialize();
}

// Destroy graph
X11Graph::~X11Graph() {
  // Save accumulated images as pdf
  if (image_names.size()) {
    const std::string pdf_name{get_next_file("cn", "pdf")};
    std::ostringstream pdf_command;
    pdf_command << "convert -quality 100 -density "
                << app.pixels_per_inch(0) << "x" << app.pixels_per_inch(1);
    for (const std::string & name : image_names) {
      pdf_command << " " << name;
    }
    pdf_command << " " << pdf_name;

    if (system(pdf_command.str().c_str()) == -1) {
      std::cerr << "Problem creating pdf file" << std::endl;
    } else {
      std::cerr << "Saved " << image_names.size() << " image"
                << (image_names.size() == 1 ? "" : "s")
                << " in pdf file " << pdf_name << std::endl;
    }
  }

  // Free all graphics contexts
  for (GC gc_ : {border_gc, border_fill_gc, minor_gc, major_gc,
          tick_label_gc}) {
    XFreeGC(display(), gc_);
  }
  for (std::vector<GC> * gcs :
    {&series_arc_gcs, &series_line_gcs, &series_radio_gcs}) {
    for (GC gc_ : *gcs) {
      XFreeGC(display(), gc_);
    }
  }
}

// Graph initialization functions
template <class ... Input>
void X11Graph::add_input(Values & x__, Values & y__, Input && ... input) {
  input_data.emplace_back(0);
  input_data.back().push_back(&x__);
  input_data.back().push_back(&y__);
  add_input(std::forward<Input>(input)...);
}

void X11Graph::add_call_back(const std::string & help_text,
                             const CallBack & call_back,
                             const bool full_draw,
                             const bool initially_on) {
  call_back_radios.reserve(100);
  call_backs.reserve(100);
  call_backs.push_back(call_back);
  call_back_radios.push_back(Radio{help_text, this,
      {1, call_backs.size() + 3.0}, {[this, full_draw]() {
          return (true || full_draw) ? draw() : redraw(); }},
                                        true, initially_on});
  radios.push_back(&call_back_radios.back());
}

void X11Graph::initialize() {
  scale.resize(3);

  // Events to watch out for
  XSelectInput(display(), window, StructureNotifyMask | ExposureMask |
               EnterWindowMask | LeaveWindowMask | KeyPressMask |
               ButtonPressMask | PointerMotionMask | ButtonReleaseMask);

  // Graphics contexts
  border_gc = create_gc(app.black, app.white, border_width,
                        LineSolid, CapButt, JoinMiter);
  border_fill_gc = create_gc(app.white, app.black, border_width,
                             LineSolid, CapButt, JoinMiter);
  minor_gc = create_gc(app.black, app.white, 1, LineOnOffDash,
                       CapButt, JoinMiter);
  major_gc = create_gc(app.black, app.white, 2, LineOnOffDash,
                       CapButt, JoinMiter);
  tick_label_gc = create_gc(app.black, app.white);

  // Series colors and names
  color_names = make_colors();
  series_names.resize(color_names.size());
  series_colors.resize(color_names.size());
  series_arc_gcs.resize(color_names.size());
  series_line_gcs.resize(color_names.size());
  series_radio_gcs.resize(color_names.size());
  for (unsigned int c{0}; c != color_names.size(); ++c) {
    series_names[c] = std::to_string(c + 1);
    std::string & color_name{color_names[c]};
    XColor & color{series_colors[c]};
    // std::cerr << c << " " << color_name << std::endl;
    if (!XAllocNamedColor(display(), app.colormap, color_name.c_str(),
                          &color, &color))
      throw Error("Could not get color") << color_name;

    // For colored arcs for points
    series_arc_gcs[c] = create_gc(color.pixel, app.white, arc_width,
                                  LineSolid, CapButt, JoinMiter);

    // For colored lines to connect points
    series_line_gcs[c] = create_gc(color.pixel, app.white, line_width,
                                   line_type, CapProjecting, JoinRound);

    // For colored thin lines to outline radios
    series_radio_gcs[c] = create_gc(color.pixel, app.white, radio_width,
                                    LineSolid, CapButt, JoinMiter);
  }

  // Create series radios, and add to master radio list, adjust properties
  series_radios.reserve(data->size());
  for (unsigned int c{0}; c != data->size(); ++c) {
    series_radios.push_back(Radio{"Pointer clicks toggle display "
            "or change colors (buttons 2,3) for series " + series_names[c],
            this, {-1, data->size() + 1.0 - c},
        {[this]() { prepare_draw(); }, [this]() { return inside; }},
            true, true, &series_radio_gcs[c]});
    saved_radios.push_back(&series_radios.back());
    series_only_arcs.emplace_back(0);
    series_only_lines.emplace_back(0);
    series_order.push_back(c);
  }
  series_order.reserve(series_order.size() + 1);

  // Add radios to master radio list
  unnamed_radios = create_unnamed_radios();

  for (std::vector<Radio> * radio_vec : {&series_radios, &unnamed_radios})
    for (Radio & radio : (*radio_vec)) radios.push_back(&radio);
  extra_radios.reserve(1000);

  // Map the window
  // XMapWindow(display(), window);
}

// Colors
std::vector<std::string> X11Graph::make_colors() const {
  std::vector<std::string> names{
    "rgb:e5/00/00", "rgb:25/00/9e", "rgb:00/b7/00", "rgb:e5/be/00",
        "rgb:06/56/93", "rgb:b7/dd/00", "rgb:e5/83/00", "rgb:95/00/95",
        "rgb:fc/7c/fc", "rgb:00/18/00", "rgb:00/fc/84", "rgb:fc/fc/a0",
        "rgb:90/a0/8c", "rgb:00/a8/fc", "rgb:74/54/fc", "rgb:fc/08/fc",
        "rgb:78/4c/30", "rgb:fc/40/78", "rgb:80/fc/68", "rgb:00/2c/fc",
        "rgb:fc/9c/78", "rgb:20/a8/68", "rgb:4c/fc/04", "rgb:d0/cc/fc",
        "rgb:70/9c/04", "rgb:00/64/30", "rgb:00/fc/e8", "rgb:70/00/00",
        "rgb:64/00/f8", "rgb:70/a8/f4", "rgb:a4/50/a0", "rgb:50/d4/ac",
        "rgb:2c/24/50", "rgb:fc/fc/34", "rgb:30/90/b8", "rgb:d0/40/24",
        "rgb:c8/40/f4", "rgb:c4/d0/5c", "rgb:ec/00/9c", "rgb:00/f0/34",
        "rgb:ac/f4/b8", "rgb:54/38/b4", "rgb:bc/78/54", "rgb:54/70/70",
        "rgb:a8/08/40", "rgb:b0/80/dc", "rgb:58/cc/3c", "rgb:24/6c/f8",
        "rgb:b4/00/e4", "rgb:38/48/00", "rgb:00/c4/bc", "rgb:cc/bc/ac",
        "rgb:e8/6c/ac", "rgb:38/d4/fc", "rgb:fc/0c/4c", "rgb:74/2c/70",
        "rgb:a0/6c/00", "rgb:28/84/00", "rgb:98/a8/40", "rgb:70/70/bc",
        "rgb:fc/6c/44", "rgb:fc/30/c4", "rgb:c0/28/78", "rgb:00/2c/bc",
        "rgb:64/00/48", "rgb:20/00/e0", "rgb:9c/2c/00", "rgb:8c/fc/24",
        "rgb:90/2c/d4", "rgb:fc/ac/d8", "rgb:e8/fc/e8", "rgb:3c/fc/58",
        "rgb:4c/90/3c", "rgb:90/c4/c4", "rgb:78/d0/00", "rgb:00/00/38",
        "rgb:00/98/34", "rgb:d8/a4/3c", "rgb:fc/d0/78", "rgb:00/24/80",
        "rgb:b0/a0/00", "rgb:40/fc/d0", "rgb:44/30/f0", "rgb:74/cc/78",
        "rgb:00/78/68", "rgb:c8/fc/7c", "rgb:fc/54/00", "rgb:60/04/b8",
        "rgb:54/24/20", "rgb:3c/54/44", "rgb:00/68/c8", "rgb:00/d4/64",
        "rgb:c8/90/90", "rgb:8c/5c/68", "rgb:b0/f8/f8", "rgb:c4/24/b8",
        "rgb:74/fc/a4", "rgb:64/6c/08", "rgb:c4/fc/3c", "rgb:3c/40/7c",
        "rgb:54/a8/90", "rgb:40/bc/08", "rgb:00/48/5c", "rgb:18/c4/34",
        "rgb:84/7c/38", "rgb:14/e4/00", "rgb:00/a0/98", "rgb:ac/a8/fc",
        "rgb:fc/4c/fc", "rgb:00/34/2c", "rgb:ac/00/04", "rgb:fc/28/14",
        "rgb:fc/c8/38", "rgb:34/00/0c", "rgb:58/04/80", "rgb:90/d8/48",
        "rgb:8c/d0/fc", "rgb:fc/d8/c8", "rgb:cc/54/74", "rgb:5c/7c/f0",
        "rgb:38/60/b0", "rgb:3c/f8/90", "rgb:3c/b0/dc", "rgb:a4/38/48",
        "rgb:e0/fc/00", "rgb:20/c8/90", "rgb:88/98/c4", "rgb:10/f0/b4",
        "rgb:18/00/68", "rgb:d0/00/68", "rgb:a8/d8/8c", "rgb:00/58/00",
        "rgb:6c/a4/60", "rgb:9c/58/d8", "rgb:6c/54/94", "rgb:00/d0/ec",
        "rgb:64/dc/dc", "rgb:28/7c/8c", "rgb:98/78/98", "rgb:1c/48/dc",
        "rgb:00/90/d4", "rgb:88/28/a0", "rgb:dc/90/c4", "rgb:40/d4/68",
        "rgb:d4/18/30", "rgb:d8/64/e0", "rgb:dc/9c/fc", "rgb:ac/5c/30",
        "rgb:dc/44/a4", "rgb:6c/40/00", "rgb:b8/a8/68", "rgb:e8/78/74",
        "rgb:bc/c0/24", "rgb:fc/44/40", "rgb:34/e8/28", "rgb:30/94/fc",
        "rgb:e0/08/d0", "rgb:90/84/68", "rgb:84/20/30", "rgb:50/54/d8",
        "rgb:d4/e4/a4", "rgb:90/14/fc", "rgb:d0/60/04", "rgb:34/1c/c4",
        "rgb:c0/80/20", "rgb:fc/a0/18", "rgb:8c/88/fc", "rgb:fc/b8/a4",
        "rgb:30/fc/fc", "rgb:dc/e0/24", "rgb:f4/f4/68", "rgb:68/84/94",
        "rgb:3c/70/24", "rgb:64/b4/c0", "rgb:60/f8/38", "rgb:2c/d8/d0",
        "rgb:cc/24/00", "rgb:c0/00/a8", "rgb:d0/18/fc", "rgb:ec/1c/78",
        "rgb:2c/78/50", "rgb:8c/0c/68", "rgb:34/00/3c", "rgb:90/08/c4",
        "rgb:fc/c8/fc", "rgb:bc/d4/d0", "rgb:b4/a4/c8", "rgb:bc/6c/b4",
        "rgb:84/f8/d0", "rgb:78/b8/24", "rgb:30/24/98", "rgb:00/04/bc",
        "rgb:2c/a0/20", "rgb:58/34/4c", "rgb:fc/e0/00", "rgb:34/b4/b0",
        "rgb:9c/40/fc", "rgb:dc/b8/7c", "rgb:30/24/00", "rgb:d4/5c/44",
        "rgb:28/60/70", "rgb:64/20/d4", "rgb:fc/90/48", "rgb:d8/38/54",
        "rgb:9c/fc/8c", "rgb:b4/64/fc", "rgb:fc/54/c8", "rgb:78/4c/c0",
        "rgb:74/30/fc", "rgb:9c/3c/78", "rgb:58/94/d0", "rgb:0c/f8/5c",
        "rgb:00/54/fc", "rgb:00/84/fc", "rgb:00/7c/a4", "rgb:a8/ec/64",
        "rgb:80/d8/a0", "rgb:1c/18/24", "rgb:68/64/4c", "rgb:fc/8c/a4",
        "rgb:30/38/2c", "rgb:44/90/68", "rgb:3c/b0/44", "rgb:bc/44/c8",
        "rgb:2c/74/d0", "rgb:a0/c0/00", "rgb:00/94/0c", "rgb:24/40/b0",
        "rgb:00/08/fc", "rgb:00/18/54", "rgb:f0/2c/f4", "rgb:3c/10/fc",
        "rgb:ac/4c/08", "rgb:b0/e0/2c", "rgb:94/8c/14", "rgb:a4/fc/00",
        "rgb:94/bc/64", "rgb:d4/b4/dc", "rgb:64/4c/6c", "rgb:60/ec/7c",
        "rgb:8c/00/20", "rgb:78/f4/00", "rgb:5c/20/98", "rgb:3c/50/fc",
        "rgb:4c/20/6c", "rgb:bc/70/84", "rgb:d8/94/64", "rgb:54/d8/14",
        "rgb:0c/38/04", "rgb:00/b4/50", "rgb:50/50/20", "rgb:b0/24/24",
        "rgb:00/b8/7c", "rgb:fc/60/88", "rgb:a4/b8/a0", "rgb:74/fc/fc"};
  if (data->size() > max_series)
    throw Error(std::string("Too many series to display (max is ") +
                std::to_string(max_series) + ")");

  if (names.size() < data->size()) {
    const unsigned int doublings{5};
    names.reserve(names.size() * pow(2, doublings));
    for (unsigned int n{0}; n != doublings; ++n) {
      if (names.size() > data->size()) break;
      names.insert(names.end(), names.begin(), names.end());
    }
  }
  names.resize(std::max(100UL, data->size()));
  return names;
}

inline void X11Graph::set_color(const unsigned int series,
                                const unsigned int color) {
  // Change GCs and redraw
  XSetForeground(display(), series_arc_gcs[series],
                 series_colors[color].pixel);
  XSetForeground(display(), series_line_gcs[series],
                 series_colors[color].pixel);
  XSetForeground(display(), series_radio_gcs[series],
                 series_colors[color].pixel);
}

inline void X11Graph::reset_colors() {
  for (unsigned int series{0}; series != series_radios.size(); ++series) {
    set_color(series, series);
  }
}

void color_change_callback(
    const unsigned int color, const unsigned int series, X11Graph & graph,
    const X11App & app__, const Window & win__) {
  // Make sure color chooser does not outlive graph!
  if (!app__.exists(win__)) {
    std::cerr << "Parent graph has exited - "
              << "color chooser is now non-functional" << std::endl;
    return;
  }

  if (color != series) graph.colors_changed = true;
  graph.set_color(series, color);
  graph.draw();
}

// Range functions
inline void X11Graph::get_range(const unsigned int a) {
  constexpr double padding{0.01};
  for (const bool y : {0, 1}) {
    if (a != 2 && a != y) continue;
    range[y] = {unset(1.0), nunset(1.0), 0};
    for (unsigned int s{0}; s != data->size(); ++s) {
      if (!series_radios[s]) continue;
      for (const double val : *(*data)[s][y]) {
        if (!std::isfinite(val)) continue;
        if (range[y][0] > val) range[y][0] = val;
        if (range[y][1] < val) range[y][1] = val;
      }
    }
    range[y][2] = range[y][1] - range[y][0];
    range[y][0] -= padding * range[y][2];
    range[y][1] += padding * range[y][2];
    range[y][2] = range[y][1] - range[y][0];
    zoomed[y] = false;
  }
  if (a == 2) max_range = range;
}

inline void X11Graph::set_range(const bool y,
                                const double low, const double high) {
  if (fabs(high - low) > 0.00000000001 * max_range[y][2]) {
    range[y][0] = low;
    range[y][1] = high;
    range[y][2] = range[y][1] - range[y][0];
  } else {
    // Reset range if screwy
    range = max_range;
    return;
  }
  zoomed[y] = (dne(range[y][0], max_range[y][0]) ||
               dne(range[y][1], max_range[y][1]));
}

inline void X11Graph::range_jump(const bool y, const double dist) {
  set_range(y, range[y][0] + dist, range[y][1] + dist);
}

inline bool X11Graph::in_range(const double x, const double y) const {
  return x >= range[0][0] && x <= range[0][1] &&
      y >= range[1][0] && y <= range[1][1];
}

inline bool X11Graph::in_range(const dPoint pos) const {
  return in_range(pos.x, pos.y);
}

inline void X11Graph::show_range(const std::string prefix) const {
  std::cout << prefix << " range";
  for (const bool y : {false, true}) {
    for (const double val : range[y]) {
      std::cout << " " << val;
    }
  }
  if (bounds.size()) {
    std::cout << " bounds";
    for (const bool y : {false, true}) {
      for (const double val : bounds[y]) {
        std::cout << " " << val;
      }
    }
  }
  std::cout << " scale";
  for (const double val : scale) {
    std::cout << " " << val;
  }
  std::cout << std::endl;
}

// Data to window coordinate transformation
inline int X11Graph::coord(const bool y, const double val) const {
  if (y) return bounds[1][1] - (val - range[1][0]) * scale[1];
  return bounds[0][0] + (val - range[0][0]) * scale[0];
}
inline double X11Graph::dcoord(const bool y, const double val) const {
  if (y) return bounds[1][1] - (val - range[1][0]) * scale[1];
  return bounds[0][0] + (val - range[0][0]) * scale[0];
}

inline Point X11Graph::coord(const dPoint point) const {
  return Point{coord(0, point.x), coord(1, point.y)};
}

inline XPoint X11Graph::xcoord(const dPoint point) const {
  XPoint result;
  result.x = coord(0, point.x);
  result.y = coord(1, point.y);
  return result;
}

inline XPoint X11Graph::xcoord(const Point point) const {
  XPoint result;
  result.x = point.x;
  result.y = point.y;
  return result;
}

// Window to data coordinate transformation
inline double X11Graph::icoord(const bool y, const int val) const {
  if (y) return (bounds[1][1] - val) / scale[1] + range[1][0];
  return (val - bounds[0][0]) / scale[0] + range[0][0];
}
inline dPoint X11Graph::icoord(const Point point) const {
  return dPoint{icoord(0, point.x), icoord(1, point.y)};
}

// Which quadrant of graph is a point in - picture an X across window
// which is two lines of positive and negative slope
inline unsigned int X11Graph::get_quadrant(const Point point) const {
  const bool below_pos{point.y > bounds[1][1] + (point.x - bounds[0][0]) *
        (bounds[1][0] - bounds[1][1]) / (bounds[0][1] - bounds[0][0])};
  const bool below_neg{point.y > bounds[1][0] + (point.x - bounds[0][0]) *
        (bounds[1][1] - bounds[1][0]) / (bounds[0][1] - bounds[0][0])};
  return below_pos ? (below_neg ? 0 : 3) : (below_neg ? 1 : 2);
}

// Border width
inline int X11Graph::min_border() const {
  return 0.05 * std::min(width(), height());
}

// Call-back functions
void X11Graph::expose(const XExposeEvent & event) {
  if (bounds.size() == 0) {
    get_range();
    prepare();
  }
  if (drawn) {
    XCopyArea(display(), pixmap, window, gc, event.x, event.y,
              event.width, event.height, event.x, event.y);
    draw_controls();
  } else {
    draw();
  }
}

void X11Graph::enter(const XCrossingEvent &) {
  inside = true;
  erase_border();
  draw_controls();
}

inline void X11Graph::key(const XKeyEvent & event) {
  // std::cerr << event.keycode << std::endl;

  // Arrow key motion
  const unsigned int arrow_codes[2][2]{{113, 114}, {116, 111}};
  for (const bool y : {false, true}) {
    if (event.keycode == arrow_codes[y][0] ||
        event.keycode == arrow_codes[y][1]) {
      const double distance{((event.state == (ShiftMask | ControlMask)) ?
                             1.0 * range[y][2] :
                             ((event.state & ShiftMask) ? 0.05 * range[y][2] :
                              ((event.state & ControlMask) ? 0.5 * range[y][2] :
                               1 / scale[y])))};
      range_jump(y, (event.keycode == arrow_codes[y][1] ? 1 : -1) * distance);
      prepare_draw();
      XSync(display(), true);
      return;
    }
  }

  KeySym sym;
  XComposeStatus compose;
  const unsigned int kBufLen{10};
  char buffer[kBufLen];
  int count{XLookupString(const_cast<XKeyEvent *>(&event),
                          buffer, kBufLen, &sym, &compose)};
  if (count == 1 && buffer[0] >= 32 && buffer[0] < 127) {
    // std::cerr << " key '" << buffer << "'" << endl;
    bool more{false};
    unsigned int rgb{0};
    switch (buffer[0]) {
      case 'R':
        more = true;
        // fall-thru
      case 'r':
        rgb = 0;
        break;
      case 'G':
        more = true;
        // fall-thru
      case 'g':
        rgb = 1;
        break;
      case 'B':
        more = true;
        // fall-thru
      case 'b':
        rgb = 2;
        break;
      case 'c':
        more = true;
        // fall-thru
      case 'C':
        rgb = 3;
        break;
      default:
        return;
    }
    const std::string RGB{"RGBC"};
    if (rgb == 3) {
      for (unsigned int r{0}; r != series_radios.size(); ++r) {
        Radio & radio{series_radios[r]};
        if (radio.contains(event)) {
          static uint64_t next_color{series_radios.size()};
          XSetForeground(display(), series_arc_gcs[r],
                         series_colors[next_color % color_names.size()].pixel);
          XSetForeground(display(), series_line_gcs[r],
                         series_colors[next_color % color_names.size()].pixel);
          XSetForeground(display(), series_radio_gcs[r],
                         series_colors[next_color % color_names.size()].pixel);
          draw();
          if (more) {
            ++next_color;
          } else {
            --next_color;
          }
          return;
        }
      }
    }
  }
}

inline void X11Graph::button_press(const XButtonEvent & event) {
  // Button actions - no work done here other than record last press
  // Inside graph: both dimensions.  Outside graph: one dimension
  //
  // key   button : press only (on release)  / drag or move (during)
  // --------------------------------------------------------------------
  // none        1: center                   / select a new view
  // shift   or  2: center and zoom in       / scroll
  // control or  3: center and zoom out      / zoom

  // Register initial click
  click = event;
  if (click == 0) return;
  last_motion = event;
  moved = false;
  small_move = false;

  // Check for series color change
  if (click == 2 || click == 3) {
    for (unsigned int r{0}; r != series_radios.size(); ++r) {
      Radio & radio{series_radios[r]};
      if (radio.contains(event)) {
        return;
      }
    }
  }

  // Check for any normal radio action
  for (Radio * radio : radios) { if (radio->press(event)) return; }
}

inline void X11Graph::motion(const XMotionEvent & event) {
  // Check for series color change
  if (click == 2 || click == 3) {
    for (unsigned int r{0}; r != series_radios.size(); ++r) {
      Radio & radio{series_radios[r]};
      if (radio.contains(click)) {
        return;
      }
    }
  }

  moved = true;
  if (XPending(display())) return;

  Event motion_event{Event::X, &app.event};
  bool call_back_acted{false};
  for (unsigned int c{0}; c != call_backs.size(); ++c) {
    if (call_back_radios[c]) {
      if (call_backs[c](*this, motion_event)) {
        call_back_acted = true;
        break;
      }
    }
  }

  if (!call_back_acted) {
    // Status text
    if (!help_radio && help_radio.contains(event)) {
      status = help_radio.description;
      return draw_status(true);
    }
    status = "";
    if (help_radio) {
      for (Radio * radio : radios) {
        // if (radio->visible()) 0;
        if (radio->contains(event)) {
          status = radio->description;
          if (!radio->visible()) status += " (inactive)";
          break;
        }
      }
      if (status.empty())
        status = long_status(in_bounds(event), get_quadrant(event) % 2);
    } else if (coord_radio && in_bounds(event)) {
      std::ostringstream coordinates;
      coordinates << std::setprecision(12) << "(";
      const Point point{event};
      for (const bool y : {false, true}) {
        const double val{icoord(y, point[y])};
        const double res{range[y][2] / bounds[y][2]};
        const double pres{pow(10, floor(log10(res)))};
        const double rval{round(val / pres) * pres};
        const double nval{log_radios[y] ? pow(10, rval) : rval};
        coordinates << (y ? " , " : " ") << nval;
      }
      coordinates << " )";
      status = coordinates.str();
    }
    draw_status();
  }
  if (event.state == 0) return;
  for (Radio * radio : radios) if (radio->contains(click)) return;

  if (click == 0) return;

  const Point point{event};
  const unsigned int quadrant{get_quadrant(click)};
  const bool y_press{(quadrant % 2) == 1};
  const Range old_range(range);

  const bool scroll{click == 2};
  const bool zoom{click == 3};
  const bool select{click == 1};

  if (scroll) {
    for (const bool y : {false, true}) {
      if (!in_bounds(click) && y_press != y) continue;
      const int distance{point[y] - last_motion[y]};
      const double move{(y ? 1 : -1) * distance / scale[y]};
      range_jump(y, move);
    }
  } else if (select) {
    // draw_controls();
    if (in_bounds(click)) {
      const Point min_point{min(last_motion.x, click.x, point.x),
            min(last_motion.y, click.y, point.y)};
      const Point max_point{max(last_motion.x, click.x, point.x),
            max(last_motion.y, click.y, point.y)};
      // Cover vertical lines
      const int y_start{min(last_motion.y, click.y)};
      const int y_height{abs(last_motion.y - click.y) + 1};
      XCopyArea(display(), pixmap, window, gc, last_motion.x, y_start,
                1, y_height, last_motion.x, y_start);
      XCopyArea(display(), pixmap, window, gc, click.x, y_start,
                1, y_height, click.x, y_start);

      // Cover horizontal lines
      const int x_start{min(last_motion.x, click.x)};
      const int x_width{abs(last_motion.x - click.x) + 1};
      XCopyArea(display(), pixmap, window, gc, x_start, last_motion.y,
                x_width, 1, x_start, last_motion.y);
      XCopyArea(display(), pixmap, window, gc, x_start, click.y,
                x_width, 1, x_start, click.y);
      XDrawRectangle(display(), window, gc,
                     min(click.x, point.x), min(click.y, point.y),
                     abs(click.x - point.x), abs(click.y - point.y));
    } else {
      const bool above(quadrant == 0 || quadrant == 3);
      const int loc{bounds[!y_press][above] + (above ? 2 : -2) * border_width};
      XDrawLine(display(), window, border_fill_gc,
                y_press ? loc : click.x, y_press ? click.y : loc,
                y_press ? loc : last_motion.x, y_press ? last_motion.y : loc);
      XDrawLine(display(), window, border_gc,
                y_press ? loc : click.x, y_press ? click.y : loc,
                y_press ? loc : point.x, y_press ? point.y : loc);
    }
  } else if (zoom) {
    for (const bool y : {false, true}) {
      if (!in_bounds(click) && y_press != y) continue;
      const int distance{point[y] - last_motion[y]};
      const double change{(y ? 1 : -1) * range[y][2] *
            distance / bounds[y][2]};
      set_range(y, range[y][0] - change, range[y][1] + change);
    }
  }
  last_motion = point;
  if (range != old_range) {
    small_move = true;
    return prepare_draw();
  }
}

inline void X11Graph::button_release(const XButtonEvent & event) {
  Click::Resetter resetter{click};
  if (click == 0) return;

  // Check for series color change
  if (click == 2 || click == 3) {
    for (unsigned int r{0}; r != series_radios.size(); ++r) {
      Radio & radio{series_radios[r]};
      if (radio.contains(click)) {
        set_window_offset();
        const int ccscale{2};
        X11Colors::create(
            app, color_names, 0, false,
            width() / ccscale, height() / ccscale,
            window_offset.x + width() - (click == 3 ? -4 : width() / ccscale),
            window_offset.y + click.y - height() / ccscale / 2,
            X11Colors::CallBack(std::bind(
                &color_change_callback, std::placeholders::_1, r,
                std::ref(*this), std::cref(app), window)),
            click == 2,
            std::string("Color chooser for series ") + series_names[r]);
        return;
      }
    }
  }

  for (Radio * radio : radios) { if (radio->release(click)) return; }

  Event button_event{Event::X, &app.event};
  for (unsigned int c{0}; c != call_backs.size(); ++c) {
    if (call_back_radios[c]) {
      if (call_backs[c](*this, button_event)) {
        return;
      }
    }
  }

  const Point release{event};
  const unsigned int quadrant{get_quadrant(click)};
  const bool y_press{(quadrant % 2) == 1};
  const Range old_range(range);

  if (moved) {
    if (click == 1) {
      // Drag event defines an x, y zoom
      for (const bool y : {false, true}) {
        if (!in_bounds(click) && y_press != y) continue;
        const double min_c{icoord(y, std::min(release[y], click[y]))};
        const double max_c{icoord(y, std::max(release[y], click[y]))};
        set_range(y, (y ? max_c : min_c), (y ? min_c : max_c));
      }
    }
    moved = false;
  } else {
    // Button 1, 2, 3 click only behavior
    const bool in{click == 2};
    const bool center{click == 1};
    if (click > 0) {
      for (const bool y : {false, true}) {
        // if ((center || out) && range[y] == max_range[y]) continue;
        if (!in_bounds(click) && y_press != y) continue;
        const double zoom{center ? 1.0 : (in ? 0.1 : 10.0)};
        const double half{0.5 * range[y][2] * zoom};
        const double mid{icoord(y, click[y])};
        set_range(y, std::max(max_range[y][0], mid - half),
                  std::min(max_range[y][1], mid + half));
      }
    }
  }
  if (range != old_range || small_move) {
    small_move = false;
    prepare_draw();
  }
}

inline void X11Graph::leave(const XCrossingEvent &) {
  inside = false;
  if (destroyed) return;
  //  for (Radio * radio : radios) radio->erase();
  status = "";
  draw_controls();
}

void X11Graph::prepare() {
  drawn = false;
  // Set graph area and clip rectangle
  const int border{min_border()};
  set_bounds(border, extent(0) - border, border, extent(1) - border);
  set_clip_rectangle(bounds[0][0], bounds[1][0], bounds[0][2], bounds[1][2]);

  // Make sure range is reasonable
  if (range[0][0] >= max_range[0][1] || range[0][1] <= max_range[0][0] ||
      range[1][0] >= max_range[1][1] || range[1][1] <= max_range[1][0]) {
    range = max_range;
  }

  // Set scales
  for (const bool y : {false, true}) { scale[y] = bounds[y][2] / range[y][2]; }
  scale[2] = scale[1] / scale[0];

  // Determine arcs and points to connect with lines to display
  arcs.resize(data->size());
  points.resize(data->size());
  const Range erange{{range[0][0] - line_width, range[0][1] + line_width},
    {range[1][0] - line_width, range[1][1] + line_width}};

  auto series_fun = [this, &erange](const unsigned int s) {
    arcs[s].clear();
    points[s].clear();
    if (!series_radios[s]) return;

    // Arc properties
    const double radius{arc_radius};
    const double diam{radius * 2};
    XArc arc;  // FN
    arc.width = diam;
    arc.height = diam;
    arc.angle1 = 0;
    arc.angle2 = 64 * 360;

    const XYSeries & series{(*data)[s]};
    for (unsigned int p{0}; p != series[0]->size(); ++p) {
      const dPoint vals{(*series[0])[p], (*series[1])[p]};
      if (std::isfinite(vals[0]) && std::isfinite(vals[1])) {
        if (in_range(vals) && do_arcs(s)) {
          for (const bool y : {false, true})
            (y ? arc.y : arc.x) = (coord(y, vals[y])) - radius;
          arcs[s].emplace_back(arc);
        }
        // Complicated to handle lines exiting the display area properly
        if (do_lines(s)) {  // Assumes points ordered by X!!!
          if (vals[0] < erange[0][0]) continue;
          if (p) {
            // left - right range transition
            const dPoint last{(*series[0])[p - 1], (*series[1])[p - 1]};
            if (last.x > erange[0][1]) break;
            if (last.x < erange[0][0] || vals.x > erange[0][1]) {
              for (const bool left : {true, false}) {
                const double x_vertical{left ? erange[0][0] : erange[0][1]};
                if (last.x >= x_vertical || vals.x <= x_vertical) continue;
                const double y_int{line_vertical_y(last, vals, x_vertical)};
                if (y_int > erange[1][0] && y_int < erange[1][1])
                  points[s].push_back(xcoord(dPoint{x_vertical, y_int}));
              }
            }
            // top-bottom transition points, properly ordered by x
            if ((last.y < erange[1][0]) != (vals.y < erange[1][0]) ||
                (last.y < erange[1][1]) != (vals.y < erange[1][1])) {
              const bool last_low{last.y < erange[1][0]};
              for (const bool high :
                {last_low ? false : true, last_low ? true : false}) {
                const double y_horizontal{high ? erange[1][1] : erange[1][0]};
                if ((last.y > y_horizontal) == (vals.y > y_horizontal))
                  continue;
                const double x_int{line_horizontal_x(last, vals, y_horizontal)};
                if (x_int >= erange[0][0] && x_int <= erange[0][1])
                  points[s].push_back(xcoord(dPoint{x_int, y_horizontal}));
              }
            }
          }
          if (vals.x >= erange[0][0] && vals.x <= erange[0][1] &&
              vals.y >= erange[1][0] && vals.y <= erange[1][1])
            points[s].push_back(xcoord(vals));
        }
      }
    }
  };

  std::vector<std::future<void>> futures;
  for (unsigned int s{0}; s != data->size(); ++s)
    futures.emplace_back(pool.run(series_fun, s));

  for (std::future<void> & result : futures) result.get();
}

void X11Graph::draw() {
  if (just_configured) {
    XFillRectangle(display(), pixmap, fill_gc, 0, 0, width(), height());
  } else {
    XFillRectangle(display(), pixmap, fill_gc,
                   bounds[0][0], bounds[1][0], bounds[0][2], bounds[1][2]);
  }

  // Handle special drawing commands
  Event pre_draw(Event::PreDraw);
  for (unsigned int c{0}; c != call_backs.size(); ++c)
    if (call_back_radios[c]) call_backs[c](*this, pre_draw);

  // The graph arcs and points to connect with lines
  const uint64_t arc_block{max_request / 3};
  const uint64_t line_block{max_request / 2};
  for (const unsigned int s : series_order) {
    if (do_arcs(s)) {
      for (unsigned int bs{0}; bs < arcs[s].size(); bs += arc_block) {
        const uint64_t n{bs + arc_block < arcs[s].size() ?
              arc_block : arcs[s].size() - bs};
        if (n) (outlines_radio ? XDrawArcs : XFillArcs)(
                display(), pixmap, series_arc_gcs[s], &arcs[s][bs],
                static_cast<unsigned int>(n));
      }
    }
    if (do_lines(s)) {
      for (unsigned int bs{0}; bs < points[s].size(); bs += line_block) {
        const uint64_t n{bs + line_block < points[s].size() ?
              line_block : points[s].size() - bs};
        if (n) XDrawLines(display(), pixmap, series_line_gcs[s],
                          &points[s][bs], static_cast<unsigned int>(n),
                          CoordModeOrigin);
      }
    }
  }
  // Handle special drawing commands
  Event nothing;
  for (unsigned int c{0}; c != call_backs.size(); ++c)
    if (call_back_radios[c]) call_backs[c](*this, nothing);

  if (just_configured) {
    just_configured = false;
    XCopyArea(display(), pixmap, window, gc, 0, 0, width(), height(), 0, 0);
    draw_controls();
  } else {
    redraw();
  }
  drawn = true;
  if (!small_move) {
    const SavedConfig current{current_config()};
    if (saved_config.empty() || current != saved_config.back()) {
      save_config(std::move(current));
      // draw_controls();
    }
  }
}

//
// Prepare and draw helpers
//

inline bool X11Graph::do_arcs(const unsigned int s) const {
  if (!series_radios[s]) return false;
  return (arcs_radio && !series_only_lines[s]) || series_only_arcs[s];
}

inline bool X11Graph::do_arcs() const {
  for (unsigned int s{0}; s != series_only_arcs.size(); ++s)
    if (do_arcs(s)) return true;
  return false;
}

inline bool X11Graph::can_do_arcs() const {
  for (unsigned int s{0}; s != series_only_arcs.size(); ++s)
    if (series_radios[s] && !series_only_lines[s]) return true;
  return false;
}

inline bool X11Graph::do_lines(const unsigned int s) const {
  if (!series_radios[s]) return false;
  return (lines_radio && !series_only_arcs[s]) || series_only_lines[s];
}

inline bool X11Graph::do_lines() const {
  for (unsigned int s{0}; s != series_only_lines.size(); ++s)
    if (do_lines(s)) return true;
  return false;
}

inline bool X11Graph::can_do_lines() const {
  for (unsigned int s{0}; s != series_only_lines.size(); ++s)
    if (series_radios[s] && !series_only_arcs[s]) return true;
  return false;
}

void X11Graph::prepare_log() {
  // A one time operation to set up
  if (log_data.size() != input_data.size()) {
    log_data = Data(input_data.size());
    log_x_data = log_y_data = input_data;
    for (unsigned int s{0}; s != input_data.size(); ++s) {
      for (const bool y : {false, true}) {
        log_series.emplace_back(
            std::make_unique<Values>(input_data[s][y]->size()));
        Values & log_values{*log_series.back()};
        for (unsigned int p{0}; p != input_data[s][y]->size(); ++p)
          log_values[p] = log10((*input_data[s][y])[p]);
        log_data[s].push_back(&log_values);
      }
      log_x_data[s][0] = log_data[s][0];
      log_y_data[s][1] = log_data[s][1];
    }
  }

  // Select data view each time (precomputed to avoid log() delay)
  if (log_radios[0] && log_radios[1]) {
    data = &log_data;
  } else if (log_radios[0]) {
    data = &log_x_data;
  } else if (log_radios[1]) {
    data = &log_y_data;
  } else {
    data = &input_data;
  }
  get_range();
}

inline std::string X11Graph::long_status(const bool in, const bool y) {
  return std::string("Pointer (1 - 2/shift - 3/control) clicks ") +
      "(center - zoom in - zoom out) at point " +
      "and drags (select - scroll - zoom) for " +
      (in ? "X and Y axes" : (y ? "Y axis" : "X axis"));
}

void X11Graph::draw_status(const bool force) const {
  XFillRectangle(display(), window, fill_gc, bounds[0][0], 0,
                 bounds[0][1] - bounds[0][0], bounds[1][0] - border_width);
  if (force || help_radio || coord_radio) {
    const double avail_height{bounds[1][0] * 0.65};
    X11Font * fits{app.fonts.fits(status, bounds[0][2], avail_height)};
    if (fits != status_font) {
      status_font = fits;
      XSetFont(display(), gc, fits->id());
    }
    XDrawString(display(), window, gc, bounds[0][0],
                fits->centered_y((bounds[1][0] - border_width) / 2),
                const_cast<char *>(status.c_str()),
                static_cast<unsigned int>(status.size()));
  }
}

void X11Graph::draw_controls() {
  XDrawRectangle(display(), window, border_gc,
                 bounds[0][0], bounds[1][0], bounds[0][2], bounds[1][2]);
  draw_status();
  draw_grid();
  for (const Radio * radio : radios) radio->draw();

  // Draw tick labels
  draw_ticks();
}

inline void X11Graph::draw_grid() const {
  for (const bool y : {false, true}) {
    const Axis axis{range[y][0], range[y][1], 3, log_radios[y]};
    for (const std::pair<double, bool> tick : axis.ticks()) {
      if (!grid_radios[!tick.second][y]) continue;
      const int loc{coord(y, tick.first)};
      XDrawLine(display(), window, tick.second ? major_gc : minor_gc,
                y ? bounds[0][0] : loc, y ? loc : bounds[1][0],
                y ? bounds[0][1] : loc, y ? loc : bounds[1][1]);
    }
  }
}

void X11Graph::draw_ticks() {
  if (inside) return;
  if (!tick_radios[0] && !tick_radios[1]) return;
  static std::vector<std::string> tick_labels;
  tick_labels.clear();
  if (0)
    std::cerr << "Bounds "
              << bounds[0][0] << " " << bounds[0][1] << " "
              << bounds[1][0] << " " << bounds[1][1] << std::endl;
  const double avail_height{bounds[1][0] * 0.6};
  X11Font * fits{app.fonts.fits("moo", bounds[0][2], avail_height)};
  if (fits != tick_font) {
    tick_font = fits;
    XSetFont(display(), tick_label_gc, fits->id());
  }
  const int t_height{tick_font->height()};
  for (const bool y : {false, true}) {
    if (!tick_radios[y]) continue;
    const Axis axis{range[y][0], range[y][1], 3, log_radios[y]};
    for (const std::pair<double, bool> tick : axis.ticks()) {
      if (!tick.second) continue;
      const int loc{coord(y, tick.first)};
      std::ostringstream label;
      label << std::setprecision(6)
            << (log_radios[y] ? pow(10, tick.first) : tick.first);
      tick_labels.push_back(label.str());
      std::string & text{tick_labels.back()};
      const int t_width{tick_font->string_width(text)};
      XDrawString(display(), window, tick_label_gc,
                  y ? std::max(0, bounds[0][0] - t_width - 3) :
                  loc - t_width / 2,
                  y ? tick_font->centered_y(loc) : bounds[1][1] + t_height,
                  text.c_str(),
                  static_cast<unsigned int>(text.size()));
    }
  }
}

inline void X11Graph::redraw() {
  XCopyArea(display(), pixmap, window, gc, bounds[0][0], bounds[1][0],
            bounds[0][2], bounds[1][2], bounds[0][0], bounds[1][0]);
  draw_controls();
}

inline void X11Graph::set_clip_rectangle(
    const unsigned int x, const unsigned int y,
    const unsigned int width_, const unsigned int height_) {
  XRectangle clip_rectangle(rect(x, y, width_, height_));
  static XRectangle last_arc_clip_rectangle(rect(0, 0, 0, 0));
  if (clip_rectangle != last_arc_clip_rectangle) {
    for (const GC gc_ : series_arc_gcs) {
      XSetClipRectangles(display(), gc_, 0, 0, &clip_rectangle, 1, YXBanded);
      last_arc_clip_rectangle = clip_rectangle;
    }
  }
  static XRectangle last_line_clip_rectangle(rect(0, 0, 0, 0));
  if (clip_rectangle != last_line_clip_rectangle) {
    for (const GC gc_ : series_line_gcs)
      XSetClipRectangles(display(), gc_, 0, 0, &clip_rectangle, 1, YXBanded);
    last_line_clip_rectangle = clip_rectangle;
  }
}

inline void X11Graph::erase_border() {
  XFillRectangle(display(), window, fill_gc,
                 0, bounds[1][1], width(), height());
  XFillRectangle(display(), window, fill_gc,
                 0, 0, bounds[0][0], height());
}

inline void X11Graph::set_line_widths(std::vector<GC> gcs, const int width_) {
  for (unsigned int g{0}; g != data->size(); ++g)
    XSetLineAttributes(display(), gcs[g], width_, LineSolid,
                       CapButt, JoinRound);
}

inline double X11Graph::line_vertical_y(const dPoint low_x, const dPoint high_x,
                                        const double x) const {
  const double slope((high_x.y - low_x.y) / (high_x.x - low_x.x));
  return low_x.y + (x - low_x.x) * slope;
}

inline double X11Graph::line_horizontal_x(
    const dPoint low_x, const dPoint high_x,
    const double y) const {
  const double slope((high_x.y - low_x.y) / (high_x.x - low_x.x));
  return low_x.x + (y - low_x.y) / slope;
}

XPoint X11Graph::line_bounds_intersection(
    const dPoint in, const dPoint out) const {
  // need to do better to include lines between points outside range
  // but intersecting the range.  also need to reduce unnecessary
  // computation here and need to place points outside of range...

  const bPoint out_high{out[0] > range[0][1], out[1] > range[1][1]};
  const bPoint out_low{out[0] < range[0][0], out[1] < range[1][0]};
  const bPoint is_out{out_high[0] || out_low[0], out_high[1] || out_low[1]};
  const dPoint limit{range[0][out_high[0]], range[1][out_high[1]]};
  if (!dne(in.x, out.x)) return xcoord(dPoint{in.x, limit.y});
  if (!dne(in.y, out.y)) return xcoord(dPoint{limit.x, in.y});
  const double slope((out.y - in.y) / (out.x - in.x));
  const dPoint solutions{
    in.x + (limit.y - in.y) / slope, in.y + (limit.x - in.x) * slope};
  const dPoint trials[2]{{solutions.x, limit.y}, {limit.x, solutions.y}};
  const dPoint distance{in.distance(trials[0]), in.distance(trials[1])};
  const bool best_is_x{(is_out[0] && is_out[1]) ? distance.x < distance.y :
        is_out[1]};
  const dPoint intersection{best_is_x ? trials[0] : trials[1]};
  return xcoord(intersection);
}

//
// Assorted functions
//
inline bool X11Graph::slow() const { return true; }

// Bug in code? sometimes movie cannot be started until after zoom out
bool X11Graph::movie(const bool right) {
  status = "Playing the movie - click movie radio button again to stop";
  using Time = std::chrono::time_point<std::chrono::system_clock>;
  const Time start_time{std::chrono::system_clock::now()};
  Time last_time{start_time};
  const double page_rate{0.35};  // pages per second scroll rate
  XEvent event;
  XWindowEvent(display(), window, ButtonReleaseMask, &event);
  const double frames_per_second{15.0};
  const size_t milliseconds_per_frame{
    static_cast<uint64_t>(1000 / frames_per_second)};
  for (uint64_t frame{0}; ; ++frame) {
    while (XCheckWindowEvent(display(), window, ButtonPressMask, &event)) {
      XWindowEvent(display(), window, ButtonReleaseMask, &event);
      if (movie_radios[right].release(event.xbutton)) {
        movie_radios[right] = false;
        return true;
      }
    }

    const std::chrono::milliseconds frame_elapsed{
      frame * milliseconds_per_frame};
    const Time frame_time{start_time + frame_elapsed};
    const Time time{std::chrono::system_clock::now()};
    if (time > frame_time) continue;
    if (time < frame_time) std::this_thread::sleep_until(frame_time);

    const double seconds{std::chrono::duration_cast<std::chrono::milliseconds>(
        frame_time - last_time).count() / 1000.0};
    const double movement{page_rate * seconds * range[0][2]};
    range_jump(0, (right ? 1 : -1) * movement);
    last_time = frame_time;
    if (!(range[0][0] > max_range[0][0] && range[0][1] < max_range[0][1])) {
      movie_radios[right] = false;
      return true;
    }
    small_move = true;
    prepare_draw();
    XSync(display(), false);
  }
}

void X11Graph::save_image(const std::string & base_name,
                          void_fun call_back) {
  if (!call_back) {
    call_back = [this]() {
      status = "Saving Image";
      draw_controls();
      draw_status(true);
      XFlush(display());
    };
  }
  inside = false;
  const bool help_state = help_radio;
  help_radio = false;
  draw_controls();
  X11Win::save_image(base_name, call_back);
  inside = true;
  help_radio = help_state;
  status = "Done saving image";
  draw_controls();
  draw_status(true);
}

//
// Radio functions
//
std::vector<Radio> X11Graph::create_unnamed_radios() {
  return std::vector<Radio>{
    {"Save an image of graph, and add all images so far to a pdf",
          this, {1, 1}, {[this]() { save_image("cn"); }}},
    {"Zoom out both axes", this, {1, -1}, {[this]() { get_range(2);
          prepare_draw(); }, [this]() { return zoomed[0] || zoomed[1]; }}},
    {"Zoom out X axis", this, {2, -1}, {[this]() {
          get_range(0); prepare_draw(); }, zoom_tester(0)}},
    {"Zoom out Y axis", this, {1, -2}, {[this]() {
          get_range(1); prepare_draw(); }, zoom_tester(1)}},
    {"Jump left X axis by one screen", this, {98.5, -1}, {[this]() {
          range_jump(0, -range[0][2]); prepare_draw(); }, zoom_tester(0)}},
    {"Jump left X axis by half a screen", this, {99.5, -1}, {[this]() {
          range_jump(0, -range[0][2] / 2); prepare_draw(); }, zoom_tester(0)}},
    {"Jump right X axis by half a screen", this, {100.5, -1}, {[this]() {
          range_jump(0, range[0][2] / 2); prepare_draw(); }, zoom_tester(0)}},
    {"Jump right X axis by one screen", this, {101.5, -1}, {[this]() {
          range_jump(0, range[0][2]); prepare_draw(); }, zoom_tester(0)}},
    {"Jump up Y axis by one screen", this, {1, 98.5}, {[this]() {
          range_jump(1, range[1][2]); prepare_draw(); }, zoom_tester(1)}},
    {"Jump up Y axis by half a screen", this, {1, 99.5}, {[this]() {
          range_jump(1, range[1][2] / 2); prepare_draw(); }, zoom_tester(1)}},
    {"Jump down Y axis by half a screen", this, {1, 100.5}, {[this]() {
          range_jump(1, -range[1][2] / 2); prepare_draw(); }, zoom_tester(1)}},
    {"Jump down Y axis by one screen", this, {1, 101.5}, {[this]() {
          range_jump(1, -range[1][2]); prepare_draw(); }, zoom_tester(1)}},
    {"Make markers bigger", this, {-1, -4.25}, {[this]() {
          arc_radius += 1; prepare_draw(); }, [this]() { return do_arcs(); }}},
    {"Make markers smaller", this, {-1, -3.25}, {[this]() {
          arc_radius -= 1; prepare_draw(); }, [this]() {
          return do_arcs() && arc_radius >= 2; }}},
    {"Make marker outlines thicker", this, {-1, -7.75}, {[this]() {
          set_line_widths(series_arc_gcs, arc_width += 1); draw(); }, [this]() {
          return do_arcs() && outlines_radio; }}},
    {"Make marker outlines thinner", this, {-1, -6.75}, {[this]() {
          set_line_widths(series_arc_gcs, arc_width -= 1); draw(); }, [this]() {
          return do_arcs() && outlines_radio && arc_width > 0; }}},
    {"Make series lines thicker", this, {-3.25, -1}, {[this]() {
          set_line_widths(series_line_gcs, line_width += 1); draw(); },
            [this]() { return do_lines(); }}},
    {"Make series lines thinner", this, {-4.25, -1}, {[this]() {
          line_width -= 1;
          set_line_widths(series_line_gcs, (line_width == 1 ? 0 : line_width));
          draw(); }, [this]() {return do_lines() && line_width >= 2; }}},
    {"Open G-Graph tutorial webpage to the GUI help section", this, {-6.25, -1},
      {[this]() { open_url("http://mumdex.com/ggraph/#gui"); }}},
    {"Set default values for color, line and marker properties", this, {-1, -1},
      {[this]() {
          arcs_radio = true; outlines_radio = false; lines_radio = false;
          arc_radius = default_arc_radius; arc_width = default_arc_width;
          line_width = default_line_width;
          set_line_widths(series_arc_gcs, arc_width);
          set_line_widths(series_line_gcs, line_width);
          reset_colors();
          prepare_draw(); }, [this]() {
          return ((do_lines() &&
                   (colors_changed || lines_radio ||
                    dne(line_width, default_line_width))) ||
                  (do_arcs() && (!arcs_radio || outlines_radio ||
                                 dne(arc_radius, default_arc_radius) ||
                                 dne(arc_width, default_arc_width)))); }}}};
}

inline bool_fun X11Graph::radio_tester(const Radio & radio, const bool state) {
  return [&radio, state]() { return radio.toggled == state; };
}

inline bool_fun X11Graph::zoom_tester(const bool y) {
  return [this, y]() { return zoomed[y]; };
}

//
// Saved configuration history
//
inline SavedConfig X11Graph::current_config() const {
  SavedConfig current{*this};
  current.radio_states.clear();
  for (Radio * radio : saved_radios) {
    current.radio_states.push_back(*radio);
  }
  return current;
}

inline void X11Graph::restore_config(const SavedConfig & config) {
  if (dne(config.line_width, line_width)) {
    set_line_widths(series_line_gcs,
                    (config.line_width == 1 ? 0 : config.line_width));
  }
  if (dne(config.arc_width, arc_width)) {
    set_line_widths(series_arc_gcs, config.arc_width);
  }
  for (unsigned int r{0}; r != saved_radios.size(); ++r) {
    saved_radios[r]->toggled = config.radio_states[r];
  }
  SavedConfig::restore_config(config);
}

inline void X11Graph::save_config(const SavedConfig & config) {
  saved_config.push_back(std::move(config));
}

inline unsigned int X11Graph::n_threads() const {
  return n_threads_;
}

inline void X11Graph::n_threads(const unsigned int n_threads__) {
  n_threads_ = n_threads__;
}

void X11Graph::open_url(const std::string & url) const {
  std::ostringstream browser;
#ifdef __APPLE__
  const std::string open_browser{"open -a safari"};
#else
#ifdef __CYGWIN__
  const std::string open_browser{
    "/cygdrive/c/Program*Files/Internet*Explorer/iexplore.exe"};
#else
  const std::string open_browser{"firefox"};
#endif
#endif
  browser << open_browser << " " << url << " &";
  if (system(browser.str().c_str()) == -1) {
    std::cerr << "Problem starting browser" << std::endl;
  }
}


//
// Text grid selector
//
class X11TextGrid : public X11Win {
 public:
  using X11Win::X11Win;
  using Column = std::vector<std::string>;
  using Data = std::vector<Column>;
  using CellStatus = std::vector<std::vector<unsigned char>>;
  using CallBack = std::function<bool (const CellStatus &)>;

  static void create(X11App & app, const Data & data_,
                     const std::vector<unsigned int> & inactive_cols_ = {},
                     const std::vector<unsigned int> & inactive_rows_ = {},
                     const std::vector<unsigned int> & exclusive_cols_ = {},
                     const std::vector<unsigned int> & exclusive_rows_ = {},
                     CallBack call_back_ = CallBack(),
                     CallBack cell_test_ = CallBack(),
                     const unsigned int width__ = 1000,
                     const unsigned int height__ = 800,
                     const int x_off__ = 0,
                     const int y_off__ = 0) {
    app.add(std::make_unique<X11TextGrid>(
        app, data_, inactive_cols_, inactive_rows_,
        exclusive_cols_, exclusive_rows_,
        call_back_, cell_test_,
        width__, height__, x_off__, y_off__));
  }

  explicit X11TextGrid(X11App & app__, const Data & data_,
                       const std::vector<unsigned int> & inactive_cols_ = {},
                       const std::vector<unsigned int> & inactive_rows_ = {},
                       const std::vector<unsigned int> & exclusive_cols_ = {},
                       const std::vector<unsigned int> & exclusive_rows_ = {},
                       CallBack call_back_ = CallBack(),
                       CallBack cell_test_ = CallBack(),
                       const unsigned int width__ = 1000,
                       const unsigned int height__ = 800,
                       const int x_off__ = 0,
                       const int y_off__ = 0) :
      X11Win(app__, width__, height__, x_off__, y_off__, false),
      data{data_.size() ? data_ : Data(1, Column{"Empty"})},
    inactive_cols{inactive_cols_}, inactive_rows{inactive_rows_},
    exclusive_cols{exclusive_cols_}, exclusive_rows{exclusive_rows_},
    cell_status_(n_cols(), std::vector<unsigned char>(n_rows(), 0)),
    max_widths(n_cols()), call_back{call_back_}, cell_test{cell_test_} {
      // Events to watch out for
      XSelectInput(display(), window,
                   StructureNotifyMask | ExposureMask |
                   ButtonPressMask | ButtonReleaseMask);

      // Fonts of various size
      fonts.reserve(max_font_size);
      for (const unsigned int s : {60, 70, 80, 90, 100, 120, 130, 140,
              150, 160, 170, 180, 190, 200, 230, 240, 250, 300, 400,
              500, 600, 700, 1000}) {
        fonts.emplace_back(display(), s);
        if (fonts.back()) {
          font_sizes.push_back(s);
        } else {
          fonts.pop_back();
        }
      }
      if (fonts.empty()) throw Error("No fonts loaded");
      font = &fonts[fonts.size() / 2];

      XSync(display(), False);
      XColor grey;
      if (!XAllocNamedColor(display(), app.colormap, "rgb:cc/cc/cc",
                            &grey, &grey)) throw Error("Could not get grey");
      grey_gc = create_gc(grey.pixel, app.white);

      prepare();
      shrink_window_to_fit();
      XMapWindow(display(), window);
    }

  X11TextGrid(const X11TextGrid &) = delete;
  X11TextGrid & operator=(const X11TextGrid &) = delete;

  void shrink_window_to_fit() {
    XWindowChanges window_params;
    XResizeWindow(display(), window, layout_width(), layout_height());
    return;
    window_params.width = layout_width();
    window_params.height = layout_height();
    XConfigureWindow(display(), window, CWWidth | CWHeight, &window_params);
  }

  virtual void configure(const XConfigureEvent & event) {
    this->X11Win::configure(event);
    if (event.width > layout_width() || event.height > layout_height())
      shrink_window_to_fit();
  }
  virtual void button_press(const XButtonEvent & event) {
    last_motion = click = event;
    if (!in_bounds(event)) {
      for (Radio * radio : radios) { if (radio->press(event)) return; }
      return;
    }

    const Point cell{inside_cell(event)};
    if (count(inactive_rows.begin(), inactive_rows.end(), cell.y) ||
        count(inactive_cols.begin(), inactive_cols.end(), cell.x)) return;
    if (!cell_status(cell) &&
        count(exclusive_cols.begin(), exclusive_cols.end(), cell.x))
      set_column_status(cell.x, false);
    if (!cell_status(cell) &&
        count(exclusive_rows.begin(), exclusive_rows.end(), cell.y))
      set_row_status(cell.y, false);
    toggle_cell_status(cell);
    draw();
  }
  virtual void motion(const XMotionEvent &) { draw(); }
  virtual void button_release(const XButtonEvent &) {
    for (Radio * radio : radios) if (radio->release(click)) return;
  }

  template <class POINT>
  Point inside_cell(const POINT & point) const {
    return Point(static_cast<unsigned int>(upper_bound(
        column_offsets.begin(), column_offsets.end(),
        point.x) - column_offsets.begin() - 1),
                 (point.y - border_padding()) / cell_height());
  }
  bool cell_status(const Point cell) const {
    return cell_status_[cell.x][cell.y];
  }
  bool cell_status(const unsigned int x, const unsigned int y) const {
    return cell_status_[x][y];
  }
  void toggle_cell_status(const Point cell) {
    cell_status_[cell.x][cell.y] = !cell_status(cell);
  }
  void set_column_status(const unsigned int col, bool status) {
    cell_status_[col].assign(cell_status_[col].size(), status);
  }
  void set_row_status(const unsigned int row, bool status) {
    for (unsigned int col{0}; col != cell_status_.size(); ++col) {
      cell_status_[col][row] = status;
    }
  }
  void clear_status() {
    for (unsigned int c{0}; c != data.size(); ++c) set_column_status(c, false);
  }
  bool cells_selected() const {
    for (unsigned int c{0}; c != data.size(); ++c)
      for (unsigned int r{0}; r != data[c].size(); ++r)
        if (cell_status(c, r)) return true;
    return false;
  }
  unsigned int n_cells_selected() const {
    unsigned int result{0};
    for (unsigned int c{0}; c != data.size(); ++c)
      for (unsigned int r{0}; r != data[c].size(); ++r)
        if (cell_status(c, r)) ++result;
    return result;
  }

  int border_padding() const { return 50; }
  int cell_padding() const { return std::max(0.3 * font->height(), 10.0); }
  unsigned int n_rows() const { return data.empty() ? 0U :
        static_cast<unsigned int>(data[0].size()); }
  unsigned int n_cols() const { return static_cast<unsigned int>(data.size()); }
  int cell_width(const unsigned int col) const {
    return 2 * cell_padding() + max_widths[col];
  }
  int cell_height() const { return 2 * cell_padding() + font->height(); }
  int grid_width() const { return grid_width_; }
  int grid_height() const { return n_rows() * cell_height(); }
  int layout_width() const { return 2 * border_padding() + grid_width(); }
  int layout_height() const { return 2 * border_padding() + grid_height(); }
  unsigned int font_index() const {
    return static_cast<unsigned int>(font - &fonts[0]); }
  unsigned int font_size() const { return font_sizes[font_index()]; }
  int column_offset(const unsigned int col) const {
    return column_offsets[col];
  }
  int row_offset(const unsigned int row) const {
    return border_padding() + row * cell_height();
  }

  bool layout() {
    // Determines paddings, column and row sizes and sees if fits in window
    for (unsigned int c{0}; c != data.size(); ++c) {
      max_widths[c] = 0;
      for (unsigned int r{0}; r != data[c].size(); ++r)
        max_widths[c] = std::max(max_widths[c], font->string_width(data[c][r]));
    }

    const unsigned int total_text_max_width{
      std::accumulate(max_widths.begin(), max_widths.end(), 0U)};

    grid_width_ = total_text_max_width + 2 * n_cols() * cell_padding();

    if (layout_width() > width()) return false;
    if (layout_height() > height()) return false;
    if (0)
      std::cerr << "Layout "
                << n_cols() << " " << n_rows() << " "
                << width() << " " << height() << " "
                << grid_width() << " " << grid_height() << " "
                << total_text_max_width << " " << font_size() << std::endl;
    return true;
  }

  virtual void prepare() {
    // Get optimal layout to just fit
    if (0) {
      while (layout()) {
        if (font == &fonts.back()) break;
        ++font;
      }
    }
    font = &fonts.back();
    while (!layout()) {
      if (font == &fonts.front()) break;
      --font;
    }

    int column_offset_{border_padding()};
    column_offsets.clear();
    for (unsigned int c{0}; c != data.size(); ++c) {
      column_offsets.push_back(column_offset_);
      column_offset_ += cell_width(c);
    }
    column_offsets.push_back(column_offset_);

    XSetFont(display(), gc, font->id());
    XSetFont(display(), fill_gc, font->id());

    set_bounds(border_padding(), border_padding() + grid_width(),
               border_padding(), border_padding() + grid_height());
  }

  virtual void draw() {
    // White page
    XFillRectangle(display(), window, fill_gc, 0, 0, width(), height());
    just_configured = false;

    std::vector<XRectangle> rectangles{
      rect(border_padding(), border_padding(), grid_width(), grid_height())};
    std::vector<XRectangle> fill_rectangles;
    for (unsigned int c{0}; c != data.size(); ++c) {
      for (unsigned int r{0}; r != data[c].size(); ++r) {
        XRectangle cell_rectangle(rect(column_offset(c), row_offset(r),
                                       cell_width(c), cell_height()));
        rectangles.push_back(cell_rectangle);
        cell_rectangle.x += 1;  // FN
        cell_rectangle.y += 1;
        cell_rectangle.width -= 1;
        cell_rectangle.height -= 1;
        if (cell_status(c, r)) fill_rectangles.push_back(cell_rectangle);
      }
    }
    XDrawRectangles(display(), window, gc, &rectangles[0],
                    static_cast<unsigned int>(rectangles.size()));
    if (fill_rectangles.size())
      XFillRectangles(display(), window, grey_gc, &fill_rectangles[0],
                      static_cast<unsigned int>(fill_rectangles.size()));
    for (unsigned int c{0}; c != data.size(); ++c)
      for (unsigned int r{0}; r != data[c].size(); ++r)
        XDrawString(display(), window, gc, column_offset(c) + cell_padding(),
                    row_offset(r) + cell_padding() + font->height(),
                    data[c][r].c_str(),
                    static_cast<unsigned int>(data[c][r].size()));
    for (const Radio * radio : radios) { radio->draw(); }
  }

  virtual ~X11TextGrid() { }

  Data data{};
  std::vector<unsigned int> inactive_cols{};
  std::vector<unsigned int> inactive_rows{};
  std::vector<unsigned int> exclusive_cols{};
  std::vector<unsigned int> exclusive_rows{};
  CellStatus cell_status_{};

  GC grey_gc{};

  static constexpr unsigned int max_font_size{60};
  std::vector<unsigned int> font_sizes{};
  std::vector<X11Font> fonts{};
  X11Font * font{};

  double border_padding_factor{0.0};
  int border_width{3};
  int cell_border_width{2};
  int grid_width_{0};
  std::vector<int> max_widths, column_offsets{};
  CallBack call_back, cell_test{};

  Point last_motion{};
  Click click{};

  Radio bigger_radio{"Bigger_text", this, {1, 98.5},
    {[this]() { }, [this]() {
        return font_index() + 1 != fonts.size(); }, [this]() {
        ++font; layout(); shrink_window_to_fit(); prepare_draw(); }}};
  Radio smaller_radio{"Bigger_text", this, {1, 99.5},
    {[this]() { }, [this]() {
        return font_index() != 0; }, [this]() {
        --font; layout(); shrink_window_to_fit(); prepare_draw(); }}};
  Radio clear_radio{"Clear all selections", this, {1, 1},
    {[this]() { clear_status(); draw(); },
          [this]() { return cells_selected(); }}};
  Radio plot_radio{"Plot selected data", this, {-1, 1},
    {[this]() { call_back(cell_status_); clear_status(); draw(); },
          [this]() { return call_back && cell_test &&
                cell_test(cell_status_); }}};
  std::vector<Radio *> radios{&clear_radio, &plot_radio};
};

class X11Plotter {
  using Names = std::vector<std::string>;
  using Col = std::vector<double>;
  using Data = std::vector<Col>;

 public:
  template <class TSV>
  explicit X11Plotter(const TSV & tsv) {
    std::vector<std::vector<std::string>> text{
      {"Data Field"}, {"Plot X"}, {"Plot Y"}};
    for (unsigned int c{0}; c != tsv.n_cols(); ++c) {
      const auto & col = tsv(c);
      if (col.is_real()) {
        names.push_back(col.name());
        text[0].push_back(col.name());
        text[1].push_back("");
        text[2].push_back("");
        data.emplace_back();
        for (unsigned int r{0}; r != tsv.n_rows(); ++r)
          if (tsv[c].is_integral()) {
            data.back().push_back(tsv.as_jitter(c, r));
          } else {
            data.back().push_back(tsv.as_real(c, r));
          }
      }
    }
    X11TextGrid::CallBack call_back{std::bind(&X11Plotter::launch_graph, this,
                                              std::placeholders::_1)};
    X11TextGrid::CallBack cell_test{std::bind(&X11Plotter::launch_ready, this,
                                              std::placeholders::_1)};
    X11TextGrid::create(app, text, {0}, {0}, {1}, {}, call_back, cell_test);
    app.run();
  }

  bool launch_graph(const X11TextGrid::CellStatus & status) {
    X11Graph::Data gd;
    X11Graph::Values * xs{&data[std::find(
        status[1].begin(), status[1].end(), 1) - status[1].begin() - 1]};
    for (unsigned int n{0}; n != names.size(); ++n)
      if (status[2][n + 1]) gd.emplace_back(X11Graph::XYSeries{xs, &data[n]});
    X11Graph & graph{X11Graph::create_whole(app, gd)};
    graph.arc_radius = 1;
    return true;
  }
  bool launch_ready(const X11TextGrid::CellStatus & status) const {
    return std::find(status[1].begin(), status[1].end(), 1) != status[1].end()
        && std::find(status[2].begin(), status[2].end(), 1) != status[2].end();
  }

  X11App app{};
  Data data{};
  Names names{};
};

#pragma GCC diagnostic pop

}  // namespace paa

#endif

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>

/** Implemented classes:
 * struct control_block;
 * template <typename T, typename Deleter> struct ptr_block;
 * template <typename T> struct obj_block;
 * template <typename T> class shared_ptr;
 * template <typename T> class weak_ptr;
 * **/

struct control_block {
  void inc(bool are_you_strong = true) {
    are_you_strong ? strong_ref++ : weak_ref++;
  }
  void dec(bool are_you_strong = true) {
    are_you_strong ? strong_ref-- : weak_ref--;

    if (are_you_strong && strong_ref == 0) {
      unlink();
    }
    if (strong_ref == 0 && weak_ref == 0) {
      delete this;
    }
  }

  uint64_t get_count(bool are_you_strong = true) {
    return are_you_strong ? strong_ref : weak_ref;
  }
  virtual ~control_block() = default;

private:
  virtual void unlink() = 0;
  uint64_t strong_ref = 1;
  uint64_t weak_ref = 0;
};

template <typename T, typename Deleter = std::default_delete<T>>
struct ptr_block : control_block, Deleter {
  T* p;

  explicit ptr_block(T* p, Deleter d = Deleter{})
      : Deleter(std::move(d)), p(p) {}

  void unlink() override {
    Deleter::operator()(p);
  }
};

template <typename T>
struct obj_block : control_block {
  template <typename... Args>
  explicit obj_block(Args&&... args) {
    new (&o) T(std::forward<Args>(args)...);
  }

  T* get() {
    return reinterpret_cast<T*>(&o);
  }

  void unlink() override {
    get()->~T();
  }

private:
  std::aligned_storage_t<sizeof(T), alignof(T)> o;
};

///--------------------------------------------------------------------------///
template <typename T>
class weak_ptr;

template <typename T>
class shared_ptr {
  control_block* cb = nullptr;
  T* obj = nullptr;

  template <typename otherT>
  friend class shared_ptr;
  friend class weak_ptr<T>;

  shared_ptr(control_block* cb, T* ptr) : cb(cb), obj(ptr) {}
  shared_ptr(const weak_ptr<T>& wp) {
    if (wp.cb && wp.cb->get_count() > 0) {
      wp.cb->inc();
      shared_ptr<T>(wp.cb, wp.obj).swap(*this);
    }
  }

public:
  shared_ptr() noexcept = default;
  explicit shared_ptr(std::nullptr_t) noexcept {}

  template <typename T_construct,
            typename Deleter = std::default_delete<T_construct>>
  explicit shared_ptr(T_construct* ptr, Deleter d = Deleter{}) {
    try {
      cb = new ptr_block<T_construct, Deleter>(ptr, std::move(d));
    } catch (...) {
      d(ptr);
      throw;
    }
    obj = ptr;
  }

  /// aliasing constructor
  template <typename T_parent>
  shared_ptr(shared_ptr<T_parent>& parent, T* ptr) : cb(parent.cb), obj(ptr) {
    if (cb)
      cb->inc();
  }

  shared_ptr(const shared_ptr<T>& other) noexcept
      : cb(other.cb), obj(other.obj) {
    if (cb)
      cb->inc();
  }
  template <typename T_copy>
  shared_ptr(const shared_ptr<T_copy>& other) noexcept
      : cb(other.cb), obj(other.obj) {
    if (cb)
      cb->inc();
  }

  shared_ptr(shared_ptr<T>&& other) noexcept : cb(other.cb), obj(other.obj) {
    other.obj = nullptr;
    other.cb = nullptr;
  }
  template <typename T_move>
  shared_ptr(shared_ptr<T_move>&& other) noexcept
      : cb(other.cb), obj(other.obj) {
    other.obj = nullptr;
    other.cb = nullptr;
  }

  shared_ptr<T>& operator=(const shared_ptr& other) noexcept {
    shared_ptr<T> new_sp(other);
    this->swap(new_sp);
    return *this;
  }
  shared_ptr<T>& operator=(shared_ptr&& other) noexcept {
    shared_ptr<T> new_sp(std::move(other));
    this->swap(new_sp);
    return *this;
  }

  T* get() const noexcept {
    return obj;
  }
  operator bool() const noexcept {
    return obj != nullptr;
  }
  T& operator*() const noexcept {
    return *obj;
  }
  T* operator->() const noexcept {
    return obj;
  }

  std::size_t use_count() const noexcept {
    return cb ? cb->get_count() : 0;
  }
  void reset() noexcept {
    if (cb)
      cb->dec();
    cb = nullptr;
    obj = nullptr;
  }

  template <typename T_reset, typename Deleter = std::default_delete<T_reset>>
  void reset(T_reset* new_ptr, Deleter d = Deleter{}) {
    // unlink();
    // make_cb(new_ptr, std::move(d));
    *this = shared_ptr(new_ptr, d);
  }

  ~shared_ptr() noexcept {
    reset();
  }

  operator T*() const noexcept {
    return obj;
  }

private:
  template <typename msT, typename... Args>
  friend shared_ptr<msT> make_shared(Args&&... args);

  void swap(shared_ptr<T>& sp) noexcept {
    std::swap(cb, sp.cb);
    std::swap(obj, sp.obj);
  }
};

template <class T, class U>
bool operator==(const shared_ptr<T>& lhs, const shared_ptr<U>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

///--------------------------------------------------------------------------///

template <typename T>
class weak_ptr {
  control_block* cb = nullptr;
  T* obj = nullptr;
  friend class shared_ptr<T>;

public:
  weak_ptr() noexcept = default;
  weak_ptr(const shared_ptr<T>& other) noexcept : cb(other.cb), obj(other.obj) {
    if (cb)
      cb->inc(false);
  }

  weak_ptr(const weak_ptr<T>& other) noexcept : cb(other.cb), obj(other.obj) {
    if (cb)
      cb->inc(false);
  }

  weak_ptr(weak_ptr<T>&& other) noexcept : cb(other.cb), obj(other.obj) {
    other.cb = nullptr;
    other.obj = nullptr;
  }

  weak_ptr<T>& operator=(const weak_ptr<T>& other) noexcept {
    weak_ptr<T> new_wp(other);
    this->swap(new_wp);
    return *this;
  }

  weak_ptr<T>& operator=(weak_ptr<T>&& other) noexcept {
    weak_ptr<T> new_wp(std::move(other));
    this->swap(new_wp);
    return *this;
  }

  weak_ptr& operator=(const shared_ptr<T>& other) noexcept {
    weak_ptr<T> new_wp(other);
    this->swap(new_wp);
    return *this;
  }

  shared_ptr<T> lock() const noexcept {
    return (*this);
  }

  ~weak_ptr() {
    if (cb)
      cb->dec(false);
  }

private:
  void swap(weak_ptr<T>& wp) noexcept {
    std::swap(cb, wp.cb);
    std::swap(obj, wp.obj);
  }
};

///--------------------------------------------------------------------------///

template <typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
  auto* ob = new obj_block<T>(std::forward<Args>(args)...);
  return shared_ptr<T>(reinterpret_cast<control_block*>(ob), ob->get());
}

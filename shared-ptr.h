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
  unsigned long strong_ref = 0;
  unsigned long weak_ref = 0;

  virtual void unlink() = 0;
  virtual ~control_block() = default;
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
private:
  std::aligned_storage<sizeof(T), alignof(T)> o[1];

public:
  template <typename... Args>
  explicit obj_block(Args&&... args) {
    new (o) T(std::forward<Args>(args)...);
  }

  T* get() {
    return reinterpret_cast<T*>(o);
  }

  void unlink() override {
    reinterpret_cast<T*>(o)->~T();
  }
};

///--------------------------------------------------------------------------///
template <typename T> class weak_ptr;

template <typename T>
class shared_ptr {
  control_block* cb = nullptr;
  T* obj = nullptr;

  template <typename otherT>
  friend class shared_ptr;

  shared_ptr(control_block* cb, T* ptr) : cb(cb), obj(ptr) {}

  template <typename T_make, typename Deleter = std::default_delete<T_make>>
  void make_cb(T_make* ptr, Deleter d = Deleter{})
  {
    try {
      cb = new ptr_block<T_make, Deleter>(ptr, std::move(d));
    } catch (...) {
      d(ptr);
      throw;
    }
    obj = ptr;
    cb->strong_ref++;
  }
  void unlink() noexcept
  {
    if (cb != nullptr) {
      if(cb->strong_ref > 0)
        cb->strong_ref--;

      if (cb->strong_ref == 0) {
        cb->unlink();
      }
      if (cb->strong_ref + cb->weak_ref == 0) {
        delete cb;
      }
      cb = nullptr;
    }
    obj = nullptr;
  }


public:
  shared_ptr() noexcept = default;
  explicit shared_ptr(std::nullptr_t) noexcept
      : cb(new ptr_block<T>(nullptr)) {}

  template <typename T_construct,
            typename Deleter = std::default_delete<T_construct>>
  explicit shared_ptr(T_construct* ptr, Deleter d = Deleter{}) {
    make_cb(ptr, std::move(d));
  }

  template <typename T_parent>
  shared_ptr(shared_ptr<T_parent>& parent, T* ptr) : cb(parent.cb), obj(ptr) {
    if (cb != nullptr)
      cb->strong_ref++;
  }

  shared_ptr(const shared_ptr<T>& other) noexcept
      : cb(other.cb), obj(other.obj) {
    if (cb != nullptr)
      cb->strong_ref++;
  }
  // Как можно исправить это место, чтобы не дублировался конструктор?
  template <typename T_copy>
  shared_ptr(const shared_ptr<T_copy>& other) noexcept
      : cb(other.cb), obj(other.obj) {
    if (cb != nullptr)
      cb->strong_ref++;
  }

  shared_ptr(shared_ptr<T>&& other) noexcept : cb(other.cb), obj(other.obj) {
    other.obj = nullptr;
    other.cb = nullptr;
  }
  // Как можно исправить это место, чтобы не дублировался конструктор?
  template <typename T_move>
  shared_ptr(shared_ptr<T_move>&& other) noexcept
      : cb(other.cb), obj(other.obj) {
    other.obj = nullptr;
    other.cb = nullptr;
  }

  shared_ptr<T>& operator=(const shared_ptr& other) noexcept{
    shared_ptr<T> new_sp(other);
    this->swap(new_sp);
    return *this;
  }
  shared_ptr<T>& operator=(shared_ptr&& other) noexcept
  {
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
    return cb ? cb->strong_ref : 0;
  }
  void reset() noexcept {
    unlink();
  }

  template <typename T_reset, typename Deleter = std::default_delete<T_reset>>
  void reset(T_reset* new_ptr, Deleter d = Deleter{}) {
    unlink();
    make_cb(new_ptr, std::move(d));
  }

  ~shared_ptr() noexcept {
    unlink();
  }

  operator T*() const noexcept {
    return obj;
  }

  bool operator==(const shared_ptr<T>& other) {
    return (cb == other.cb) && (obj == other.obj);
  }

private:
  template <typename msT, typename... Args>
  friend shared_ptr<msT> make_shared(Args&&... args);

  friend class weak_ptr<T>;

  void swap(shared_ptr<T>& sp) noexcept {
    std::swap(cb, sp.cb);
    std::swap(obj, sp.obj);
  }
};

///--------------------------------------------------------------------------///

template <typename T>
class weak_ptr {
  control_block* cb = nullptr;
  T* obj = nullptr;

public:
  weak_ptr() noexcept = default;
  weak_ptr(const shared_ptr<T>& other) noexcept : cb(other.cb), obj(other.obj) {
    if (other.cb)
      cb->weak_ref++;
  }

  weak_ptr(const weak_ptr<T>& other) noexcept : cb(other.cb), obj(other.obj) {
    if (cb != nullptr)
      cb->weak_ref++;
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
    unlink();

    cb = other.cb;
    obj = other.obj;
    if (other.cb)
      cb->weak_ref++;

    return *this;
  }

  shared_ptr<T> lock() const noexcept {
    if (cb != nullptr && cb->strong_ref != 0) {
      cb->strong_ref++;
      return shared_ptr<T>(cb, obj);
    } else {
      return shared_ptr<T>();
    }
  }

  ~weak_ptr() {
    unlink();
  }

private:
  void unlink() noexcept {
    if (cb != nullptr) {
      cb->weak_ref--;
      if (cb->strong_ref + cb->weak_ref == 0)
        delete cb;
    }

    cb = nullptr;
    obj = nullptr;
  }

  void swap(weak_ptr<T>& wp) noexcept {
    std::swap(cb, wp.cb);
    std::swap(obj, wp.obj);
  }
};

///--------------------------------------------------------------------------///

template <typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
  auto* ob = new obj_block<T>(std::forward<Args>(args)...);
  ob->strong_ref++;
  return shared_ptr(reinterpret_cast<control_block*>(ob), ob->get());
}

//template <typename T>
//template <typename T_make, typename Deleter>
//void shared_ptr<T>::make_cb(T_make* ptr, Deleter d) {
//  try {
//    cb = new ptr_block<T_make, Deleter>(ptr, std::move(d));
//  } catch (...) {
//    d(ptr);
//    throw;
//  }
//  obj = ptr;
//  cb->strong_ref++;
//}
//
//template <typename T>
//void shared_ptr<T>::unlink() noexcept {
//  if (cb != nullptr) {
//    if(cb->strong_ref > 0)
//      cb->strong_ref--;
//
//    if (cb->strong_ref == 0) {
//      cb->unlink();
//    }
//    if (cb->strong_ref + cb->weak_ref == 0) {
//      delete cb;
//    }
//    cb = nullptr;
//  }
//  obj = nullptr;
//}
//
//template <typename T>
//shared_ptr<T>& shared_ptr<T>::operator=(const shared_ptr& other) noexcept {
//  shared_ptr<T> new_sp(other);
//  this->swap(new_sp);
//  return *this;
//}
//
//template <typename T>
//shared_ptr<T>& shared_ptr<T>::operator=(shared_ptr&& other) noexcept {
//  shared_ptr<T> new_sp(std::move(other));
//  this->swap(new_sp);
//  return *this;
//}

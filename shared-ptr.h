#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>

struct control_block {
  unsigned long strong_ref = 0;
  unsigned long weak_ref = 0;

  virtual ~control_block() = default;
  virtual void unlink() = 0;
};

template <typename T>
struct ptr_block : control_block {
  T* p;

  explicit ptr_block(T* p) : p(p) {}

  void unlink() override {
    delete p;
  }
};

template <typename T>
struct obj_block : control_block {
  std::aligned_storage<sizeof(T), alignof(T)> o[1];

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

template <typename T>
class weak_ptr;

template <typename T>
class shared_ptr {
  control_block* cb = nullptr;
  T* obj = nullptr;

public:
  shared_ptr() noexcept = default;
  explicit shared_ptr(std::nullptr_t) noexcept
      : cb(new ptr_block<T>(nullptr)) {}
  explicit shared_ptr(T* ptr) {
    make_cb(ptr);
  }

  shared_ptr(const shared_ptr& other) noexcept : cb(other.cb), obj(other.obj) {
    if (cb)
      cb->strong_ref++;
  }
  shared_ptr(shared_ptr&& other) noexcept : cb(other.cb), obj(other.obj) {
    other.obj = nullptr;
    other.cb = nullptr;
  }
  shared_ptr<T>& operator=(const shared_ptr& other) noexcept;
  shared_ptr<T>& operator=(shared_ptr&& other) noexcept;

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
  void reset(T* new_ptr) {
    unlink();
    make_cb(new_ptr);
  }

  void unlink() noexcept;
  ~shared_ptr() noexcept {
    unlink();
  }

private:
  template <typename msT, typename... Args>
  friend shared_ptr<msT> make_shared(Args&&... args);

  friend class weak_ptr<T>;

  shared_ptr(control_block* cb, T* ptr) : cb(cb), obj(ptr) {}

  void make_cb(T* ptr);
  void swap(shared_ptr<T>& sp) noexcept {
    std::swap(cb, sp.cb);
    std::swap(obj, sp.obj);
  }
};

template <typename T>
class weak_ptr {
public:
  weak_ptr() noexcept = default;
  weak_ptr(const shared_ptr<T>& other) noexcept : cb(other.cb), obj(other.obj) {
    if (other.cb)
      cb->weak_ref++;
  }

  weak_ptr& operator=(const shared_ptr<T>& other) noexcept {
    this->~weak_ptr();

    cb = other.cb;
    obj = other.obj;
    if (other.cb)
      cb->weak_ref++;

    return *this;
  }

  shared_ptr<T> lock() const noexcept {
    return (cb && cb->strong_ref != 0) ? shared_ptr<T>(cb, obj)
                                       : shared_ptr<T>();
  }

  ~weak_ptr() {
    if (cb)
      cb->weak_ref--;
    cb = nullptr;
    obj = nullptr;
  }

private:
  control_block* cb = nullptr;
  T* obj = nullptr;
};

template <typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
  auto* ob = new obj_block<T>(std::forward<Args>(args)...);
  ob->strong_ref++;
  return shared_ptr(ob, ob->get());
}

template <typename T>
void shared_ptr<T>::make_cb(T* ptr) {
  try {
    cb = new ptr_block<T>(ptr);
  } catch (...) {
    delete ptr;
    throw;
  }
  obj = ptr;
  cb->strong_ref++;
}

template <typename T>
void shared_ptr<T>::unlink() noexcept {
  if (cb) {
    cb->strong_ref--;
    if (cb->strong_ref == 0)
      cb->unlink();
    if (cb->strong_ref + cb->weak_ref == 0)
      delete cb;
    cb = nullptr;
  }
  obj = nullptr;
}

template <typename T>
shared_ptr<T>& shared_ptr<T>::operator=(const shared_ptr& other) noexcept {
  shared_ptr<T> new_sp(other);
  this->swap(new_sp);
  return *this;
}

template <typename T>
shared_ptr<T>& shared_ptr<T>::operator=(shared_ptr&& other) noexcept {
  shared_ptr<T> new_sp(std::move(other));
  this->swap(new_sp);
  return *this;
}
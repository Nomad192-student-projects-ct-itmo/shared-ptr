#pragma once

#include <cstddef>
#include <iostream>
#include <utility>

// template <typename T> // нельзя из-за алиасинга
struct control_block // without 2 alloc
{
  // unsigned long long
  size_t strong_ref = 0;
  size_t weak_ref = 0;

  virtual ~control_block() = default;
  virtual void unlink() = 0;
  // virtual void get_ptr() = 0; //не жалко добавить ещё, вместо T *view
  // //нельзя из-за алиасинга
};

template <typename T>
struct ptr_block : control_block {
  ptr_block(T* p) : p(p) {}

  T* p;

  void unlink() {
    delete p;
    p = nullptr; //можно не писать
  }

  ~ptr_block() {
    unlink();
  }
};

template <typename T>
struct obj_block : control_block {
  // T o; // bad, double delete
  // aligned_storage <5, 2>; // занимает ровно столько же места, читабельней
  // std optional; // больше места занимает чем надо
  alignas(alignof(T)) char o[sizeof(T)]; // менее читабельней

  template <typename... Args>
  obj_block(Args&&... args) {
    new (o) T(std::forward<Args>(args)...);
  }

  T* get() {
    return reinterpret_cast<T*>(o);
  }

  void unlink() {
    reinterpret_cast<T*>(o)->~T();
  }
};

template <typename T>
class weak_ptr;

template <typename T>
class shared_ptr {
public:
  shared_ptr() noexcept;
  shared_ptr(nullptr_t) noexcept;
  shared_ptr(T* ptr_);

  shared_ptr(const shared_ptr& other) noexcept;
  shared_ptr(shared_ptr&& other) noexcept;
  shared_ptr<T>& operator=(const shared_ptr& other) noexcept;
  shared_ptr<T>& operator=(shared_ptr&& other) noexcept;

  T* get() const noexcept;
  operator bool() const noexcept;
  T& operator*() const noexcept;
  T* operator->() const noexcept;

  std::size_t use_count() const noexcept;
  void reset() noexcept;
  void reset(T* new_ptr);

  ~shared_ptr() noexcept;

private:
  template <typename msT, typename... Args>
  friend shared_ptr<msT> make_shared(Args&&... args);

  friend class weak_ptr<T>;

  shared_ptr(control_block* cb, T* ptr) : cb(cb), obj(ptr) {}
  control_block* cb;
  T* obj;

  shared_ptr<T>& sp_copy(const shared_ptr& other) noexcept;
  shared_ptr<T>& sp_move(shared_ptr&& other) noexcept;
  void make_cb(T* ptr);
};

template <typename T>
class weak_ptr {
public:
  weak_ptr() noexcept {}
  weak_ptr(const shared_ptr<T>& other) noexcept {
    cb = other.cb;
    obj = other.obj;
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
  obj_block<T>* ob = new obj_block<T>(std::forward<Args>(args)...);
  ob->strong_ref++;
  return shared_ptr(ob, ob->get());
}

template <typename T>
shared_ptr<T>::shared_ptr() noexcept {
  cb = nullptr;
  obj = nullptr;
}

template <typename T>
shared_ptr<T>::shared_ptr(nullptr_t) noexcept {
  cb = nullptr; // cb
  obj = nullptr;
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
shared_ptr<T>::shared_ptr(T* ptr) {
  make_cb(ptr);
}

template <typename T>
shared_ptr<T>& shared_ptr<T>::sp_copy(const shared_ptr& other) noexcept {
  obj = other.obj;
  cb = other.cb;
  if (cb)
    cb->strong_ref++;

  return *this;
}

template <typename T>
shared_ptr<T>& shared_ptr<T>::sp_move(shared_ptr&& other) noexcept {
  obj = other.obj;
  other.obj = nullptr;

  cb = other.cb;
  other.cb = nullptr;

  return *this;
}

template <typename T>
shared_ptr<T>::shared_ptr(const shared_ptr& other) noexcept {
  sp_copy(other);
}

template <typename T>
shared_ptr<T>::shared_ptr(shared_ptr&& other) noexcept {
  sp_move(std::move(other));
}

template <typename T>
shared_ptr<T>::~shared_ptr() noexcept {
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
  if (this != &other) {
    this->~shared_ptr();
    return sp_copy(other);
  } else
    return *this;
}

template <typename T>
shared_ptr<T>& shared_ptr<T>::operator=(shared_ptr&& other) noexcept {
  if (this != &other) {
    this->~shared_ptr();
    return sp_move(std::move(other));
  } else
    return *this;
}

template <typename T>
T* shared_ptr<T>::get() const noexcept {
  return obj;
}

template <typename T>
shared_ptr<T>::operator bool() const noexcept {
  return obj != nullptr;
}

template <typename T>
T& shared_ptr<T>::operator*() const noexcept {
  return *obj;
}

template <typename T>
T* shared_ptr<T>::operator->() const noexcept {
  return obj;
}

template <typename T>
std::size_t shared_ptr<T>::use_count() const noexcept {
  return cb ? cb->strong_ref : 0;
}

template <typename T>
void shared_ptr<T>::reset() noexcept {
  this->~shared_ptr();
}

template <typename T>
void shared_ptr<T>::reset(T* new_ptr) {
  this->~shared_ptr();
  make_cb(new_ptr);
}
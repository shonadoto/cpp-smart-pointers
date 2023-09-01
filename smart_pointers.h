#pragma once

#include <memory>
template <typename T>
class WeakPtr;

template <typename T>
class EnableSharedFromThis;

template <typename T>
class SharedPtr;

struct BaseControlBlock {
    size_t shared_cnt;
    size_t weak_cnt;
    BaseControlBlock(int shared_cnt, int weak_cnt)
        : shared_cnt(shared_cnt), weak_cnt(weak_cnt) {}
    virtual void useDeleter() = 0;
    virtual void Destroy() = 0;
    virtual void Deallocate() = 0;
    virtual ~BaseControlBlock() = default;
};

template <typename T>
class SharedPtr {

    template <typename U>
    friend class WeakPtr;

    template <typename U>
    friend class SharedPtr;

    template <typename Deleter, typename Alloc>
    struct ControlBlockRegular : public BaseControlBlock {

        [[no_unique_address]] Deleter deleter;
        [[no_unique_address]] Alloc alloc;
        T* ptr;

        ControlBlockRegular(T* ptr, Deleter deleter, Alloc alloc)
            : BaseControlBlock(1, 0),
              deleter(deleter),
              alloc(alloc),
              ptr(ptr) {}

        void useDeleter() override {
            deleter(ptr);
        }

        void Destroy() override {}

        void Deallocate() override {
            using AllocControlBlock = typename std::allocator_traits<
                Alloc>::template rebind_alloc<ControlBlockRegular>;
            AllocControlBlock new_alloc = alloc;
            std::allocator_traits<AllocControlBlock>::deallocate(new_alloc,
                                                                 this, 1);
        }

        ~ControlBlockRegular() override = default;
    };

    template <typename Alloc>
    struct ControlBlockMakeShared : public BaseControlBlock {

        [[no_unique_address]] Alloc alloc;
        T data;

        template <typename... Args>
        ControlBlockMakeShared(Alloc alloc, Args&&... args)
            : BaseControlBlock(1, 0),
              alloc(alloc),
              data(std::forward<Args>(args)...) {}

        void useDeleter() override {}

        void Destroy() override {
            std::allocator_traits<Alloc>::destroy(alloc, &data);
        }

        void Deallocate() override {
            using AllocControlBlock = typename std::allocator_traits<
                Alloc>::template rebind_alloc<ControlBlockMakeShared>;

            AllocControlBlock new_alloc = alloc;
            std::allocator_traits<AllocControlBlock>::deallocate(new_alloc,
                                                                 this, 1);
        }

        ~ControlBlockMakeShared() override = default;
    };

    BaseControlBlock* cb;
    T* ptr;

  public:
    SharedPtr() : cb(nullptr), ptr(nullptr) {}

    template <typename Deleter = std::default_delete<T>,
              typename Alloc = std::allocator<T>>
    SharedPtr(T* ptr, Deleter deleter = Deleter(), Alloc alloc = Alloc())
        : cb(nullptr), ptr(ptr) {

        using AllocControlBlock = typename std::allocator_traits<
            Alloc>::template rebind_alloc<ControlBlockRegular<Deleter, Alloc>>;

        AllocControlBlock new_alloc = alloc;
        cb = std::allocator_traits<AllocControlBlock>::allocate(new_alloc, 1);
        new (cb) ControlBlockRegular<Deleter, Alloc>(ptr, deleter, alloc);

        if constexpr (std::is_base_of_v<EnableSharedFromThis<T>, T>) {
            ptr->wptr = *this;
        }
    }

    SharedPtr(const SharedPtr& other) : cb(other.cb), ptr(other.ptr) {
        if (cb != nullptr) {
            ++cb->shared_cnt;
        }
    }

    SharedPtr(SharedPtr&& other) : cb(other.cb), ptr(other.ptr) {
        other.ptr = nullptr;
        other.cb = nullptr;
    }

    template <typename Derived>
    SharedPtr(const SharedPtr<Derived>& other)
        : cb(static_cast<BaseControlBlock*>(other.cb)),
          ptr(static_cast<T*>(other.ptr)) {
        if (cb != nullptr) {
            ++cb->shared_cnt;
        }
    }

    template <typename Derived>
    SharedPtr(SharedPtr<Derived>&& other)
        : cb(static_cast<BaseControlBlock*>(other.cb)),
          ptr(static_cast<T*>(other.ptr)) {
        other.ptr = nullptr;
        other.cb = nullptr;
    }

    ~SharedPtr() {
        if (cb == nullptr) {
            return;
        }
        --cb->shared_cnt;
        if (cb->shared_cnt == 0) {
            cb->useDeleter();
            cb->Destroy();
            if (cb->weak_cnt == 0) {
                cb->Deallocate();
            }
        }
        cb = nullptr;
        ptr = nullptr;
    }

  private:
    template <typename Alloc>
    SharedPtr(ControlBlockMakeShared<Alloc>* cb) : cb(cb), ptr(&cb->data) {}

    SharedPtr(const WeakPtr<T>& weak) : cb(weak.cb), ptr(weak.ptr) {
        ++cb->shared_cnt;
    }

  public:
    void swap(SharedPtr& other) {
        std::swap(cb, other.cb);
        std::swap(ptr, other.ptr);
    }

    SharedPtr& operator=(const SharedPtr& other) {
        if (this == &other) {
            return *this;
        }
        SharedPtr tmp(other);
        swap(tmp);
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) {
        if (this == &other) {
            return *this;
        }
        SharedPtr tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    template <typename Derived>
    SharedPtr& operator=(const SharedPtr<Derived>& other) {
        if (static_cast<void*>(this) == static_cast<const void*>(&other)) {
            return *this;
        }
        SharedPtr tmp(other);
        swap(tmp);
        return *this;
    }

    template <typename Derived>
    SharedPtr& operator=(SharedPtr<Derived>&& other) {
        if (static_cast<void*>(this) == static_cast<void*>(&other)) {
            return *this;
        }
        SharedPtr tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    template <typename Deleter = std::default_delete<T>,
              typename Alloc = std::allocator<T>>
    void reset(T* ptr, Deleter deleter = Deleter(), Alloc alloc = Alloc()) {
        SharedPtr tmp(ptr, deleter, alloc);
        swap(tmp);
    }
    void reset() {
        SharedPtr tmp;
        swap(tmp);
    }

    size_t use_count() const {
        return cb->shared_cnt;
    }

    template <typename U, typename... Args>
    friend SharedPtr<U> makeShared(Args&&... args);

    template <typename U, typename Alloc, typename... Args>
    friend SharedPtr<U> allocateShared(Alloc alloc, Args&&... args);

    T& operator*() const {
        return *ptr;
    }
    T* get() const {
        return ptr;
    }
    T* operator->() const {
        return ptr;
    }
};

template <typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(Alloc alloc, Args&&... args) {
    using AllocControlBlock =
        typename std::allocator_traits<Alloc>::template rebind_alloc<
            typename SharedPtr<T>::template ControlBlockMakeShared<Alloc>>;
    AllocControlBlock new_alloc = alloc;
    auto* cb = std::allocator_traits<AllocControlBlock>::allocate(new_alloc, 1);
    std::allocator_traits<AllocControlBlock>::construct(
        new_alloc, cb, alloc, std::forward<Args>(args)...);
    return SharedPtr<T>(cb);
}

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {

    return allocateShared<T>(std::allocator<T>(), std::forward<Args>(args)...);
}

template <typename T>
class WeakPtr {

    template <typename U>
    friend class WeakPtr;

    template <typename U>
    friend class SharedPtr;

    BaseControlBlock* cb;
    T* ptr;

  public:
    WeakPtr() : cb(nullptr), ptr(nullptr) {}
    WeakPtr(const SharedPtr<T>& shared) : cb(shared.cb), ptr(shared.ptr) {
        ++cb->weak_cnt;
    }

    WeakPtr(SharedPtr<T>&& shared) : cb(shared.cb), ptr(shared.ptr) {
        shared.ptr = nullptr;
        shared.cb = nullptr;
        --cb->shared_cnt;
        ++cb->weak_cnt;
    }

    template <typename Derived>
    WeakPtr(const SharedPtr<Derived>& shared)
        : cb(static_cast<BaseControlBlock*>(shared.cb)),
          ptr(static_cast<T*>(shared.ptr)) {
        ++cb->weak_cnt;
    }

    template <typename Derived>
    WeakPtr(SharedPtr<Derived>&& shared)
        : cb(static_cast<BaseControlBlock*>(shared.cb)),
          ptr(static_cast<T*>(shared.ptr)) {
        shared.ptr = nullptr;
        shared.cb = nullptr;
        --cb->shared_cnt;
        ++cb->weak_cnt;
    }

    WeakPtr(const WeakPtr& other) : cb(other.cb), ptr(other.ptr) {
        ++cb->weak_cnt;
    }
    WeakPtr(WeakPtr&& other) : cb(other.cb), ptr(other.ptr) {
        other.cb = nullptr;
        other.ptr = nullptr;
    }

    template <typename Derived>
    WeakPtr(const WeakPtr<Derived>& shared)
        : cb(static_cast<BaseControlBlock*>(shared.cb)),
          ptr(static_cast<T*>(shared.ptr)) {
        ++cb->weak_cnt;
    }

    template <typename Derived>
    WeakPtr(WeakPtr<Derived>&& shared)
        : cb(static_cast<BaseControlBlock*>(shared.cb)),
          ptr(static_cast<T*>(shared.ptr)) {
        ++cb->weak_cnt;
    }

    ~WeakPtr() {
        if (cb == nullptr) {
            return;
        }
        --cb->weak_cnt;
        if (cb->weak_cnt == 0 && cb->shared_cnt == 0) {
            cb->Deallocate();
        }
        cb = nullptr;
        ptr = nullptr;
    }

    void swap(WeakPtr& other) {
        std::swap(cb, other.cb);
        std::swap(ptr, other.ptr);
    }

    WeakPtr& operator=(const WeakPtr& other) {
        WeakPtr tmp(other);
        swap(tmp);
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& other) {
        WeakPtr tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    template <typename Derived>
    WeakPtr operator=(const SharedPtr<Derived>& shared) {
        WeakPtr tmp(shared);
        swap(tmp);
        return *this;
    }

    template <typename Derived>
    WeakPtr operator=(SharedPtr<Derived>&& shared) {
        WeakPtr tmp(std::move(shared));
        swap(tmp);
        return *this;
    }

    template <typename Derived>
    WeakPtr operator=(const WeakPtr<Derived>& shared) {
        WeakPtr tmp(shared);
        swap(tmp);
        return *this;
    }

    template <typename Derived>
    WeakPtr operator=(WeakPtr<Derived>&& shared) {
        WeakPtr tmp(std::move(shared));
        swap(tmp);
        return *this;
    }

    bool expired() const noexcept {
        return cb == nullptr || cb->shared_cnt == 0;
    }

    SharedPtr<T> lock() const {
        return SharedPtr<T>(*this);
    }

    T& operator*() const {
        return *ptr;
    }
    T* get() const {
        return ptr;
    }
    T* operator->() const {
        return ptr;
    }
    size_t use_count() const {
        return cb->shared_cnt;
    }
};

template <typename T>
class EnableSharedFromThis {
    WeakPtr<T> wptr;

  public:
    SharedPtr<T> shared_from_this() const noexcept {
        return wptr.lock();
    }
};

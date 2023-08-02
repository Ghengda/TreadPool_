#ifndef THREADPOOL_H
#define THREADPOOL_H


#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>





enum class PoolMode
{
	MODE_FIXED,  
	MODE_CACHED, 
};

// 线程类型
class Thread
{
public:
	
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	
	~Thread() = default;

	void start();

	int getId()const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;  
};


// 线程池类型

class ThreadPool
{
public:

	ThreadPool();

	~ThreadPool();

	void setMode(PoolMode mode);

	void setTaskQueMaxThreshHold(int threshhold);

	void setThreadSizeThreshHold(int threshhold);

	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) ->std::future<decltype(func(args...))>;
	
	void start(int initThreadSize = std::thread::hardware_concurrency());

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	void threadFunc(int threadid);

	bool checkRunningState() const;

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;

	int initThreadSize_;  
	int threadSizeThreshHold_; 
	std::atomic_int curThreadSize_;
	std::atomic_int idleThreadSize_; 

	
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;
	std::atomic_int taskSize_; 
	int taskQueMaxThreshHold_;  

	std::mutex taskQueMtx_; 
	std::condition_variable notFull_;
	std::condition_variable notEmpty_; 
	std::condition_variable exitCond_; 

	PoolMode poolMode_; 
	std::atomic_bool isPoolRunning_;
};
//模板函数一般定义在头文件
//提交任务至任务队列
template<typename Func, typename... Args>
 auto ThreadPool::submitTask(Func&& func, Args&&... args)->std::future<decltype(func(args...))>
{
	
	using RType = decltype(func(args...));
	auto task = std::make_shared<std::packaged_task<RType()>>(
		std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
	std::future<RType> result = task->get_future();

	
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
	{
		
		std::cerr << "task queue is full, submit task fail." << std::endl;
		auto task = std::make_shared<std::packaged_task<RType()>>(
			[]()->RType { return RType(); });
		(*task)();
		return task->get_future();
	}

	
	taskQue_.emplace([task]() {(*task)(); });
	taskSize_++;

	
	notEmpty_.notify_all();

	
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << ">>> create new thread..." << std::endl;

		
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		
		threads_[threadId]->start();
		
		curThreadSize_++;
		idleThreadSize_++;
	}

	
	return result;
}

#endif

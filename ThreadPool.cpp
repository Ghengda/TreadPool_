#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 2; 
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; 

//////线程池类
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}



void ThreadPool::start(int initThreadSize )
{
	isPoolRunning_ = true;

	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	for (int i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}


	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); 
		idleThreadSize_++;   
	}
}

int Thread::generateId_ = 0;
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	// 所有任务必须执行完成，线程池才可以回收所有线程资源
	for (;;)
	{
		Task task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id()
				<< "尝试获取任务..." << std::endl;

			// 锁 + 双重判断
			while (taskQue_.size() == 0)
			{
				if (!isPoolRunning_)
				{
					threads_.erase(threadid); // std::this_thread::getid()
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
						<< std::endl;
					exitCond_.notify_all();
					return;
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}
				}
				else
				{
					notEmpty_.wait(lock);
				}
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "获取任务成功..." << std::endl;

			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			notFull_.notify_all();
		} 
		if (task != nullptr)
		{
			task();
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();
	}
}

// 检查pool的运行状态
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

// 开启线程池

////Thread类实现
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}

// 启动线程
void Thread::start()
{

	std::thread t(func_, threadId_);  
	t.detach(); // 设置分离线程  

}
// 获取线程id
int Thread::getId()const
{
	return threadId_;
}
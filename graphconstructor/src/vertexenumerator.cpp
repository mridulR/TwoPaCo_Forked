#include <set>
#include <deque>
#include <ctime>
#include <queue>
#include <memory>
#include <bitset>
#include <numeric>
#include <cassert>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <unordered_set>

#include <boost/ref.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_sort.h>

#include <sparsehash/sparse_hash_set>

#include "lib/SpookyV2.h"

#include "vertexenumerator.h"

namespace Sibelia
{
	const size_t VertexEnumerator::INVALID_VERTEX = -1;

	namespace
	{
		void PutInBloomFilter(ConcurrentBitVector & filter, const std::vector<uint64_t> & seed, const DnaString & item)
		{
			for (size_t i = 0; i < seed.size(); i++)
			{
				uint64_t body = item.GetBody();
				uint64_t hvalue = SpookyHash::Hash64(&body, sizeof(body), seed[i]) % filter.Size();
				filter.SetConcurrently(hvalue);
			}
		}

		bool IsInBloomFilter(const ConcurrentBitVector & filter, const std::vector<uint64_t> & seed, const DnaString & item)
		{
			for (size_t i = 0; i < seed.size(); i++)
			{
				uint64_t body = item.GetBody();
				uint64_t hvalue = SpookyHash::Hash64(&body, sizeof(body), seed[i]) % filter.Size();
				if (!filter.Get(hvalue))
				{
					return false;
				}
			}

			return true;
		}

		struct Task
		{
			bool isFinal;
			uint64_t start;
			std::string str;
#ifdef _DEBUG
			static const size_t TASK_SIZE = 36;
#else
			static const size_t TASK_SIZE = 1 << 18;
#endif			
			static const size_t GAME_OVER = SIZE_MAX;
			Task() {}
			Task(uint64_t start, bool isFinal, std::string && str) :  start(start), isFinal(isFinal), str(std::move(str)) {}
		};

		DnaString kmer;			

		const size_t QUEUE_CAPACITY = 16;
		typedef boost::lockfree::spsc_queue<Task> TaskQueue;
		typedef std::unique_ptr<TaskQueue> TaskQueuePtr;

		std::string TempFile(size_t record)
		{
			std::stringstream ss;
			ss << record << ".bin";
			return ss.str();
		}

		uint64_t NormHash(const std::vector<uint64_t> & seed, DnaString posVertex, DnaString negVertex)
		{
			uint64_t hvalue = UINT64_MAX;
			DnaString kmer[] = { posVertex, negVertex };
			for (size_t i = 0; i < 2; i++)
			{
				uint64_t body = kmer[i].GetBody();
				hvalue = std::min(hvalue, SpookyHash::Hash64(&body, sizeof(body), seed[0]));
			}

			return hvalue;
		}

		bool Within(uint64_t hvalue, uint64_t low, uint64_t high)
		{
			return hvalue >= low && hvalue <= high;
		}

		DnaString Generate(std::string::const_iterator it, size_t size)
		{
			DnaString str;
			for (size_t i = 0; i < size; i++)
			{
				str.AppendBack(*(it + i));
			}

			return str;
		}

		class Record
		{
		public:

			enum Status
			{
				candidate,
				bifurcation,
				deleted,
			};

			Record() {}
			Record(DnaString posVertex, DnaString negVertex, char posPrev, char posNext, Status status = candidate)
			{

			}

			Record(DnaString canVertex, char prev, char next, Status status = candidate)
			{

			}

			Record(uint64_t body, size_t vertexLength)
			{

			}

			uint64_t GetBody() const
			{
				return 0;
			}

			DnaString GetVertex() const
			{
				return vertex_;
			}

			char GetPrev() const
			{
				return prev_;
			}

			char GetNext() const
			{
				return next_;
			}

			Status GetStatus() const
			{
				return candidate;
			}

			static Record Deleted()
			{
				return Record(DnaString(), 'A', 'C', deleted);
			}

		private:
			DnaString vertex_;
			char prev_;
			char next_;
			char status_;
		};		

		class RecordHashFunction
		{
		public:
			RecordHashFunction(size_t vertexLength)
			{

			}

		private:
		};

		class RecordEquality
		{
		public:
			RecordEquality(size_t vertexLength)
			{

			}

		private:
		};

		typedef google::sparse_hash_set<uint64_t, RecordHashFunction, RecordEquality> RecordSet;
		typedef std::queue<std::vector<Record> > RecordQueue;

		void CandidateCheckingWorker(uint64_t low,
			uint64_t high,
			const std::vector<uint64_t> & seed,
			const ConcurrentBitVector & bitVector,
			size_t vertexLength,
			TaskQueue & taskQueue,
			RecordQueue & recordQueue,
			boost::mutex & outMutex)
		{
			std::vector<size_t> hf(seed.size());
			std::vector<Record> output;
			while (true)
			{
				Task task;
				if (taskQueue.pop(task))
				{ 					
					if (task.start == Task::GAME_OVER)
					{
						break;
					}

					if (task.str.size() < vertexLength)
					{
						continue;
					}
					
					if (task.start == 0)
					{
						DnaString posVertex = Generate(task.str.begin(), vertexLength);
						DnaString negVertex = posVertex.RevComp();
						if (Within(NormHash(seed, posVertex, negVertex), low, high))
						{
							output.push_back(Record(posVertex, negVertex, 'A', 'A'));
							output.push_back(Record(posVertex, negVertex, 'C', 'A'));
						}
					}

					if (task.isFinal)
					{
						DnaString posVertex = Generate(task.str.end() - vertexLength, vertexLength);
						DnaString negVertex = posVertex.RevComp();
						if (Within(NormHash(seed, posVertex, negVertex), low, high))
						{
							output.push_back(Record(posVertex, negVertex, 'A', 'A'));
							output.push_back(Record(posVertex, negVertex, 'C', 'A'));
						}
					}

					if (task.str.size() >= vertexLength + 2)
					{
						DnaString posVertex = Generate(task.str.begin() + 1, vertexLength);	
						DnaString negVertex = posVertex.RevComp();
						for (size_t pos = 1;; ++pos)
						{
							char posPrev = task.str[pos - 1];
							char posExtend = task.str[pos + vertexLength];
							assert(posVertex.RevComp() == negVertex);
							uint64_t hvalue = UINT64_MAX;
							DnaString kmer[] = { posVertex, negVertex };
							for (size_t i = 0; i < 2; i++)
							{
								uint64_t body = kmer[i].GetBody();
								hvalue = std::min(hvalue, SpookyHash::Hash64(&body, sizeof(body), seed[0]));
							}

							if (hvalue >= low && hvalue <= high)
							{
								size_t inCount = 0;
								size_t outCount = 0;
								for (int i = 0; i < DnaString::LITERAL.size() && inCount < 2 && outCount < 2; i++)
								{
									char nextCh = DnaString::LITERAL[i];
									char revNextCh = DnaString::Reverse(nextCh);
									if (nextCh == posPrev)
									{
										++inCount;
									}
									else
									{
										DnaString posInEdge = posVertex;
										DnaString negInEdge = negVertex;
										posInEdge.AppendFront(nextCh);
										negInEdge.AppendBack(DnaString::Reverse(nextCh));
										if ((posInEdge.GetBody() <= negInEdge.GetBody() && IsInBloomFilter(bitVector, seed, posInEdge)) ||
											(negInEdge.GetBody() < posInEdge.GetBody() && IsInBloomFilter(bitVector, seed, negInEdge)))
										{
											inCount++;
										}
									}

									if (nextCh == posExtend)
									{
										++outCount;
									}
									else
									{
										DnaString posOutEdge = posVertex;
										DnaString negOutEdge = negVertex;
										posOutEdge.AppendBack(nextCh);
										negOutEdge.AppendFront(DnaString::Reverse(nextCh));
										if ((posOutEdge.GetBody() <= negOutEdge.GetBody() && IsInBloomFilter(bitVector, seed, posOutEdge)) ||
											(negOutEdge.GetBody() < posOutEdge.GetBody() && IsInBloomFilter(bitVector, seed, negOutEdge)))
										{
											outCount++;
										}
									}
								}

								if (inCount > 1 || outCount > 1)
								{
									output.push_back(Record(posVertex, negVertex, posExtend, posPrev));
								}
							}

							if (pos + vertexLength + 1 < task.str.size())
							{
								posVertex.AppendBack(posExtend);								
								negVertex.AppendFront(DnaString::Reverse(posExtend));
								posVertex.PopFront();
								negVertex.PopBack();
							}
							else
							{
								break;
							}
						}
						
					}

					if (output.size() > 0)
					{
						boost::lock_guard<boost::mutex> guard(outMutex);
						recordQueue.push(std::move(output));
					}
				}
			}
		}

		void CountingWorker(uint64_t low, uint64_t high, const std::vector<uint64_t> & seed, ConcurrentBitVector & bitVector, size_t edgeLength, TaskQueue & taskQueue)
		{
			std::vector<size_t> hf(seed.size());
			while (true)
			{
				Task task;
				if (taskQueue.pop(task))
				{
					if (task.start == Task::GAME_OVER)
					{
						break;
					}

					if (task.str.size() < edgeLength)
					{
						continue;
					}

					char ch;
					DnaString posEdge;
					for (size_t j = 0; j < edgeLength - 1; j++)
					{
						posEdge.AppendBack(task.str[j]);
					}

					DnaString negEdge = posEdge.RevComp();
					for (size_t pos = 0; pos + edgeLength - 1 < task.str.size(); pos++)
					{
						ch = task.str[pos + edgeLength - 1];
						posEdge.AppendBack(ch);
						negEdge.AppendFront(DnaString::Reverse(ch));
						assert(posEdge.RevComp() == negEdge);

						size_t k = 0;
						size_t hit = 0;
						DnaString kmer[2][2] = { { posEdge, negEdge }, { posEdge, negEdge } };
						for (size_t i = 0; i < 2 && !hit; i++, k++)
						{
							kmer[i][k].PopBack();
							kmer[i][1 - k].PopFront();
							uint64_t hvalue = UINT64_MAX;
							assert(kmer[i][0] == kmer[i][1].RevComp());
							for (size_t j = 0; j < 2 && !hit; j++)
							{
								uint64_t body = kmer[i][j].GetBody();
								hvalue = std::min(hvalue, SpookyHash::Hash64(&body, sizeof(body), seed[0]));
							}

							hit += (hvalue >= low && hvalue <= high) ? 1 : 0;
						}

						if (hit)
						{
							if (posEdge.GetBody() < negEdge.GetBody())
							{
								PutInBloomFilter(bitVector, seed, posEdge);
							}
							else
							{
								PutInBloomFilter(bitVector, seed, negEdge);
							}
							
						}

						posEdge.PopFront();
						negEdge.PopBack();
					}
				}
			}
		}

		void AggregationWorker(RecordSet & records, RecordQueue & recordQueue, boost::mutex & queueMutex, size_t vertexLength)
		{
			while (true)
			{
				std::vector<Record> candidate;
				{
					boost::lock_guard<boost::mutex> guard(queueMutex);
					if (recordQueue.size() > 0)
					{
						candidate = std::move(recordQueue.front());
						recordQueue.pop();
						if (candidate.empty())
						{
							return;
						}
					}
				}

				for (Record & record : candidate)
				{
					RecordSet::iterator it = records.find(record.GetVertex().GetBody());
					if (it == records.end())
					{
						records.insert(record.GetBody());
					}
					else
					{
						Record oldRecord(*it, )
					}
				}
			}
		}

		void DistributeTasks(const std::vector<std::string> & fileName, size_t overlapSize, std::vector<TaskQueuePtr> & taskQueue)
		{
			for (size_t file = 0; file < fileName.size(); file++)
			{
				size_t record = 0;
				const std::string & nowFileName = fileName[file];
				for (StreamFastaParser parser(nowFileName); parser.ReadRecord(); record++)
				{
					char ch;
					std::string buf;
					uint64_t prev = 0;
					uint64_t start = 0;
					bool over = false;
					do
					{
						over = !parser.GetChar(ch);
						if (!over)
						{
							start++;
							buf.push_back(ch);
						}

						if (buf.size() >= overlapSize && (buf.size() == Task::TASK_SIZE || over))
						{
							for (bool found = false; !found;)
							{
								for (TaskQueuePtr & q : taskQueue)
								{
									if (q->write_available() > 0)
									{
										std::string overlap;
										if (!over)
										{
											overlap.assign(buf.end() - overlapSize, buf.end());
										}

										q->push(Task(prev, over, std::move(buf)));
										prev = start - overlapSize + 1;
										buf.swap(overlap);
										found = true;
										break;
									}
								}
							}
						}

					} while (!over);
				}
			}

			for (size_t i = 0; i < taskQueue.size(); i++)
			{
				while (taskQueue[i]->write_available() == 0)
				{
					boost::this_thread::sleep_for(boost::chrono::nanoseconds(1000000));
				}

				taskQueue[i]->push(Task(Task::GAME_OVER, true, std::string()));
			}
		}
		
	}

	VertexEnumerator::VertexEnumerator(const std::vector<std::string> & fileName,
		size_t vertexLength,
		size_t filterSize,
		size_t hashFunctions,
		size_t rounds,
		size_t threads,
		size_t aggregationThreads,
		const std::string & tmpFileName) :
		vertexSize_(vertexLength)
	{
		std::cout << "Threads = " << threads << std::endl;
		std::cout << "Aggregation threads = " << aggregationThreads << std::endl;
		std::cout << "Hash functions = " << hashFunctions << std::endl;
		std::cout << "Filter size = " << filterSize << std::endl;
		std::cout << "Files: " << std::endl;
		for (const std::string & fn : fileName)
		{
			std::cout << fn << std::endl;
		}

		std::cout << std::string(80, '-') << std::endl;

		if (vertexLength > 30)
		{
			throw std::runtime_error("The vertex size is too large");
		}

		std::vector<uint64_t> seed(hashFunctions);
		std::generate(seed.begin(), seed.end(), rand);
		size_t edgeLength = vertexLength + 1;
		uint64_t low = 0;
		for (size_t round = 0; round < rounds; round++)
		{			
			time_t mark = time(0);
			size_t totalRecords = 0;
			uint64_t high = round == rounds - 1 ? UINT64_MAX : (UINT64_MAX / rounds) * (round + 1);
			{
				std::vector<std::unique_ptr<ConcurrentBitVector> > isCandidBit;
				{
					std::vector<TaskQueuePtr> taskQueue;
					ConcurrentBitVector bitVector(filterSize);		
					std::vector<boost::thread> workerThread(threads);
					std::cout << "Round " << round << ", " << low << ":" << high << std::endl;
					std::cout << "Counting\tEnumeration\tAggregation" << std::endl;
					for (size_t i = 0; i < workerThread.size(); i++)
					{
						taskQueue.push_back(TaskQueuePtr(new TaskQueue(QUEUE_CAPACITY)));
						workerThread[i] = boost::thread(CountingWorker,
							low,
							high,
							boost::cref(seed),
							boost::ref(bitVector),
							edgeLength,
							boost::ref(*taskQueue[i]));
					}

					DistributeTasks(fileName, edgeLength, taskQueue);				
					for (size_t i = 0; i < workerThread.size(); i++)
					{				
						workerThread[i].join();
					}
					
					std::cout << time(0) - mark << "\t";
					mark = time(0);					
					boost::mutex mutex;
					RecordQueue recordQueue;
					for (size_t i = 0; i < workerThread.size(); i++)
					{
						workerThread[i] = boost::thread(CandidateCheckingWorker,
							low,
							high,
							boost::cref(seed),
							boost::cref(bitVector),
							vertexLength,
							boost::ref(*taskQueue[i]),
							boost::ref(recordQueue),
							boost::ref(mutex));
					}

					RecordSet records(1 << 20, RecordHashFunction(vertexLength), RecordEquality(vertexLength));
					boost::thread aggregator(AggregationWorker, boost::ref(records), boost::ref(recordQueue), boost::ref(mutex), vertexLength);
					DistributeTasks(fileName, vertexLength + 1, taskQueue);
					for (size_t i = 0; i < taskQueue.size(); i++)
					{
						workerThread[i].join();
					}

					boost::lock_guard<boost::mutex> guard(mutex);
					recordQueue.push(std::vector<Record>());
					aggregator.join();

					for (uint64_t rec : records)
					{
						Record record(rec, vertexLength);
						if (record.GetStatus() == Record::bifurcation)
						{
							bifurcation_.push_back(record.GetVertex().GetBody());
						}
					}
				}
			
				std::cout << time(0) - mark << "\t";				
			}

			std::cout << time(0) - mark << std::endl;
			std::cout << "Vertex count = " << bifurcation_.size() << std::endl;
			std::cout << std::string(80, '-') << std::endl;
			low = high + 1;
		}
		
		std::sort(bifurcation_.begin(), bifurcation_.end());
	}

	size_t VertexEnumerator::GetVerticesCount() const
	{
		return bifurcation_.size();
	}

	size_t VertexEnumerator::GetId(const DnaString & vertex) const
	{
		DnaString check[2] = { vertex, vertex.RevComp() };
		for (DnaString str : check)
		{
			tbb::concurrent_vector<uint64_t>::const_iterator it = std::lower_bound(bifurcation_.begin(), bifurcation_.end(), str.GetBody());
			if (it != bifurcation_.end() && *it == str.GetBody())
			{
				return it - bifurcation_.begin();
			}
		}

		return INVALID_VERTEX;
	}
}
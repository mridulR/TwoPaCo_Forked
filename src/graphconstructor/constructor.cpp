#include <set>
#include <ctime>
#include <string>
#include <vector>
#include <memory>
#include <cassert>
#include <cstdint>
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <sstream>

#include <tclap/CmdLine.h>

#include <tbb/concurrent_queue.h>
#include "../common/graphdump/graphdump.h"
#include "../common/junctionapi/junctionapi.h"

#include "test.h"
#include "assemblyedgeconstructor.h"


size_t Atoi(const char * str)
{
	size_t ret;
	std::stringstream ss(str);
	ss >> ret;
	return ret;
}

class OddConstraint : public TCLAP::Constraint <unsigned int>
{
public:
	~OddConstraint()
	{

	}

	std::string description() const
	{
		return "value of K must be odd";
	}

	std::string shortID() const
	{
		return "oddc";
	}

	bool check(const unsigned & value) const
	{
		return (value % 2) == 1;
	}
};

int main(int argc, char * argv[])
{
	OddConstraint constraint;
	try
	{
		TCLAP::CmdLine cmd("Program for construction of the condensed de Bruijn graph from complete genomes", ' ', "0.9.2");
		
		TCLAP::ValueArg<unsigned int> kvalue("k",
			"kvalue",
			"Value of k",
			false,
			25,
			&constraint,
			cmd);

		TCLAP::ValueArg<uint64_t> filterSize("f",
			"filtersize",
			"Size of the filter",
			true,
			0,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> hashFunctions("q",
			"hashfnumber",
			"Number of hash functions",
			false,
			5,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> rounds("r",
			"rounds",
			"Number of computation rounds",
			false,
			1,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> threads("t",
			"threads",
			"Number of worker threads",
			false,
			1,
			"integer",
			cmd);

		TCLAP::ValueArg<std::string> tmpDirName("",
			"tmpdir",
			"Temporary directory name",
			false,
			".",
			"directory name",
			cmd);
		
		TCLAP::SwitchArg runTests("",
			"test",
			"Run tests",				
			cmd);

		TCLAP::UnlabeledMultiArg<std::string> fileName("filenames",
			"FASTA file(s) with nucleotide sequences.",
			true,
			"fasta files with genomes",
			cmd);

		TCLAP::ValueArg<std::string> outFileName("o",
			"outfile",
			"Output file name prefix",
			false,
			"de_bruijn.bin",
			"file name",
			cmd);

		TCLAP::ValueArg<std::string> gfa1("m",
			"gfa1",
			"output file name",
			false,
			"myfile.gfa1",
			"file name",
			cmd);


		cmd.parse(argc, argv);		
		using TwoPaCo::Range;
		if (runTests.getValue())
		{
			TwoPaCo::RunTests(10, 20, 9000, 6, Range(3, 11), Range(1, 2), Range(1, 5), Range(4, 5), 0.05, 0.1, tmpDirName.getValue());
			return 0;
		}

		tbb::concurrent_queue<TwoPaCo::JunctionPosition> queue;
		std::atomic<bool> * done = new std::atomic<bool>(false);

		std::cout << "Testing started --- > " << (*done).load(std::memory_order_relaxed) << std::endl;
    			
		/*for( int i=0; i<10; ++i ) {
        		queue.push(i);
		}
    
		typedef concurrent_queue<int>::iterator iter;
    		for( iter i(queue.unsafe_begin()); i!=queue.unsafe_end(); ++i ) {
        		cout << *i << " test ------ ";
		}
    		cout << endl;
		*/
		
		std::unique_ptr<TwoPaCo::VertexEnumerator> vid = TwoPaCo::CreateEnumerator(fileName.getValue(),
			kvalue.getValue(), filterSize.getValue(),
			hashFunctions.getValue(),
			rounds.getValue(),
			threads.getValue(),
			tmpDirName.getValue(),
			outFileName.getValue(),
			std::cout,
			&queue,
			done);

		std::cout << "Testing done --- > " << (*done).load(std::memory_order_relaxed) << std::endl;
		std::cout << "Testing done size --- > " << queue.unsafe_size() << std::endl;
		
		if (gfa1.isSet()) {
                        char* argv[8];
			argv[0] = "graphdump";
			argv[1] = "-f";
			argv[2] = "gfa1";
			argv[3] = "-k";
			argv[4] = new char[10];
			std::ostringstream oss;
			oss << kvalue.getValue();
			strcpy(argv[4], oss.str().c_str());
			argv[5] = new char[outFileName.getValue().length() + 1];
			strcpy(argv[5], outFileName.getValue().c_str());
			argv[6] = "-s";
			argv[7] = new char[fileName.getValue()[0].length() + 1];
			strcpy(argv[7], fileName.getValue()[0].c_str());
                        run_graph_dump(8, argv, gfa1.getValue(), &queue, done);

		} 
		
		if (vid) {
			std::cout << "Distinct junctions = " << vid->GetVerticesCount() << std::endl;
			std::cout << std::endl;
		}
		
	}
	catch (TCLAP::ArgException & e)
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
	catch (std::runtime_error & e)
	{
		std::cerr << "error: " << e.what() << std::endl;
		return 1;
	}
	
	return 0;
}

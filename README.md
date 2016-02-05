(Updated 5th Feb 2016)

This is the code that implements the algorithm described in the paper
"An efficient  algorithm to build the compacted de Bruijn graph from many complete genomes"

To compile the code, you need the following (Linux only):

* CMake 
* A GCC compiler supporting C++11
* Intel TBB library properly installed on your system. In other words, G++
  should be able to find TBB libs 

Once you've got all the things above, do the following:

* Go to the "build" directory
* Type cmake ../src
* Type make

This will make two targets: grahphconstructor and graphdump

graphconstructor
----------------

To run the graph construction (assuming you're in the "build/graphconstructor" dir), type:

	./graphconstructor -q <number_of_hash_functions> -f <filter_size> -k <value_of_k> --tmpdir <directory_for_temporary_files> -o <output_file> -r <number_of_rounds> <input_files>

You can also type "./graphconstructor --help" to get parameter description.
Note that the size of the Bloom filter (in bits) is actualy "2^filter_size".
The output file is a binary file that indicates junction positions on the positive strand.
The file consists of pairs of numbers, where the first number is 4 bytes long, and the second is 8 bytes long.
The first number indicates position and the second one indicates the ID of the junction.
Positions appear in the file in the same order they appear in the input genomes
This way, one can obtain all multi-edges of the graph with a linear scan.
The negative strand can be obtained easily since it is symmetric.
To make the output human readable, use the "graphdump" utility.

graphdump
---------

This utility turns the binary file into human readable one. Just run

	graphdump <input_file>

It will output a text file to the standard output.
The i-th line line corresponds to the i-th junction and is of format:

	<seq_id0> <pos0>; <seq_id1> <pos1>; ....

Where each pair "<seq_id> <pos>" corresponds to an occurence of the junction in
sequence <seq_id> at position <pos>. Sequence ids are just the numbers of sequences
in the order they appear in the input.

A note: this is a prototype version of the graph constructor. This output format is
mostly for testing purposes and not final. The release version will likely use the
GFA format, see: https://github.com/pmelsted/GFA-spec/issues/7
#include <cstdint>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cassert>
#include <fstream>
#include <boost/program_options.hpp>

#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <fenv.h>
#include <setjmp.h>

#include "loadelf.hh"
#include "helper.hh"
#include "disassemble.hh"
#include "interpret.hh"
#include "basicBlock.hh"
#include "region.hh"
#include "regionCFG.hh"
#include "perfmap.hh"
#include "debugSymbols.hh"
#include "globals.hh"
#include "simPoints.hh"

extern const char* githash;
int sArgc = -1;
char** sArgv = nullptr;

namespace globals {
  int sArgc = 0;
  char** sArgv = nullptr;
  bool isMipsEL = false;
  llvm::CodeGenOpt::Level regionOptLevel = llvm::CodeGenOpt::Aggressive;
  bool countInsns = true;
  bool simPoints = false;
  bool replay = false;
  uint64_t simPointsSlice = 0;
  uint64_t nCfgCompiles = 0;
  region *regionFinder = nullptr;
  basicBlock *cBB = nullptr;
  execUnit *currUnit = nullptr;
  bool enClockFuncts = false;
  bool enableCFG = true;
  bool verbose = false;
  bool ipo = true;
  bool fuseCFGs = true;
  bool enableBoth = true;
  uint32_t enoughRegions = 5;
  bool dumpIR = false;
  bool splitCFGBBs = true;
  uint64_t nFuses = 0;
  uint64_t nAttemptedFuses = 0;
  std::string blobName;
  uint64_t icountMIPS = 500;
  cfgAugEnum cfgAug = cfgAugEnum::none;
  std::string binaryName;
}

perfmap* perfmap::theInstance = nullptr;
std::set<regionCFG*> regionCFG::regionCFGs;
uint64_t regionCFG::icnt = 0;
uint64_t regionCFG::iters = 0;
std::map<uint32_t, basicBlock*> basicBlock::bbMap;
std::map<uint32_t, basicBlock*> basicBlock::insMap;
std::map<uint32_t, uint64_t> basicBlock::insInBBCnt;


#if ((LLVM_VERSION_MAJOR==3 && LLVM_VERSION_MINOR > 8) || (LLVM_VERSION_MAJOR > 3))
llvm::LLVMContext globalContext;
#endif


/* Locals */
static llvm::CodeGenOpt::Level optLevels[4] = {llvm::CodeGenOpt::None,
					       llvm::CodeGenOpt::Less,
					       llvm::CodeGenOpt::Default,
					       llvm::CodeGenOpt::Aggressive};

static cfgAugEnum augLevels[4] = {cfgAugEnum::none,
				  cfgAugEnum::head,
				  cfgAugEnum::aggressive,
				  cfgAugEnum::insane};
  
static state_t *s = nullptr;
int buildArgcArgv(const char *filename, const std::string &sysArgs, char ** &argv);

static jmp_buf jenv;

static void catchUnixSignal(int sig) {
  switch(sig)
    {
    case SIGFPE:
      std::cerr << KRED << "\ncaught SIGFPE!\n" << KNRM;
      if(globals::currUnit) {
	globals::currUnit->info();
      }
      exit(-1);
      break;
    case SIGINT:
      std::cerr << KRED << "\ncaught SIGINT!\n" << KNRM;
      s->brk = 1;
      longjmp(jenv, 1);
      break;
    default:
      break;
    }

}


int main(int argc, char *argv[]) {
  static_assert(sizeof(mips_t)==4, "mips union borked");
  namespace po = boost::program_options; 
  std::cerr << KGRN
	    << "MIPS BT : built "
	    << __DATE__ << " " << __TIME__
	    << ",hostname="<<gethostname()
	    << ",pid="<< getpid() << "\n"
	    << "git hash=" << githash
	    << KNRM << "\n";


  globals::binaryName = strip_path(argv[0]);
  llvm::InitializeNativeTarget();
  llvm::InitializeAllTargetMCs(); 
  llvm::InitializeNativeTargetAsmPrinter(); 
  llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 5
  llvm::llvm_start_multithreaded(); 
#endif

  char **sysArgv = nullptr;
  int sysArgc = 0;

  size_t hotThresh = 500,cl = 8;
 
  uint8_t *mem = nullptr;
  uint32_t entry_p = 0;

  uint32_t optidx = 3, augidx = 1;
  double estart=0,estop=0;
  bool report=false, hash=false, fp_exception=false, replay = false;
  uint64_t max_icnt = 0;
  std::string sysArgs, filename, simPointsFname;
  po::options_description desc("Options");
  po::variables_map vm;
  desc.add_options() 
   ("help", "Print help messages") 
   ("args,a", po::value<std::string>(&sysArgs), "arguments to mips binary")
   ("both,b", po::value<bool>(&globals::enableBoth)->default_value(true), "use regionCFG when fp regs are in state both")
   ("clock,c", po::value<bool>(&globals::enClockFuncts)->default_value(false), "enable wall-clock")
   ("cfg,d", po::value<bool>(&globals::enableCFG)->default_value(true), "enable cfg-level opt")
   ("enoughRegions,e", po::value<uint32_t>(&globals::enoughRegions)->default_value(5), "how many times does each region need to get executed")    
   ("file,f", po::value<std::string>(&filename), "mips binary")
   ("hash,h", po::value<bool>(&hash)->default_value(false), "hash memory at end of execution")
   ("ipo,i", po::value<bool>(&globals::ipo)->default_value(true), "allow jr,jal,jalr in regions")
   ("lines,l", po::value<size_t>(&cl)->default_value(8), "region-cache lines")
   ("opt,o", po::value<uint32_t>(&optidx)->default_value(3), "how much llvm code optimization")
   ("replay,p", po::value<bool>(&replay)->default_value(false), "replay binary")
   ("report,r", po::value<bool>(&report)->default_value(false), "report stats at end of execution")
   ("hotThresh,t", po::value<size_t>(&hotThresh)->default_value(500), "hot bb threshold")    
   ("verbose,v", po::value<bool>(&globals::verbose)->default_value(false), "print debug information")
   ("fp_exception", po::value<bool>(&fp_exception)->default_value(false), "fp exception")
   ("aug", po::value<uint32_t>(&augidx)->default_value(1), "how much cfg augmentation")
   ("countInsns", po::value<bool>(&globals::countInsns)->default_value(true), "CFG code generation emits insns counts")
   ("simPoints", po::value<bool>(&globals::simPoints)->default_value(false), "log for sim points")
   ("simPointsSlice", po::value<uint64_t>(&globals::simPointsSlice)->default_value(1UL<<24), "sim points slice")
   ("simPointsFname", po::value<std::string>(&simPointsFname), "sim points output file name")
   ("max_icnt", po::value<uint64_t>(&max_icnt)->default_value(~0UL), "max icnt")
   ("splitCFGBBs",po::value<bool>(&globals::splitCFGBBs)->default_value(false), "split CFG basicblocks")
   ("blobName", po::value<std::string>(&globals::blobName)->default_value("blob.bin"), "binary blob name")
   ("icountMIPS", po::value<uint64_t>(&globals::icountMIPS)->default_value(500), "millions of of instructions per second for time calculation")
   ("dumpIR",po::value<bool>(&globals::dumpIR)->default_value(false), "dump IR");
    
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm); 
  }
  catch(po::error &e) {
    std::cerr << KRED << "command-line error : " << e.what() << KNRM << "\n";
    return -1;
  }
  if(vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }
  
  globals::regionOptLevel = optLevels[optidx&3];
  globals::cfgAug = augLevels[augidx&3];
  
  if(globals::simPoints) {
    globals::countInsns = true;
  }
  if(simPointsFname.empty()) {
    simPointsFname = filename + "_" + std::to_string(rand()) + ".sp";
  }
  
#ifndef __APPLE__
  if(fp_exception) {
    feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
    signal(SIGFPE, catchUnixSignal);
  }
  signal(SIGINT, catchUnixSignal);
#endif
  
  /* Build argc and argv */
  sysArgc = buildArgcArgv(filename.c_str(),sysArgs,sysArgv);

  globals::sArgc = sysArgc;
  globals::sArgv = sysArgv;

  s = new state_t;
  assert(s);
  initState(s);

#ifdef __linux__
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
#ifdef __amd64__
		     (21 << MAP_HUGE_SHIFT) |
#endif
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
#else
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS , -1, 0);
#endif
  assert(mempt != reinterpret_cast<void*>(-1));
  assert(madvise(mempt, 1UL<<32, MADV_DONTNEED)==0);
  mem = reinterpret_cast<uint8_t*>(mempt);
  if(mem == nullptr) {
    std::cerr << globals::binaryName << ": couldn't allocate backing memory!\n";
    exit(-1);
  }
  s->mem = mem;
  
  std::map<uint32_t, std::pair<std::string, uint32_t>> syms;
  if(not(load_elf(filename.c_str(), entry_p, syms, s->mem))){
    return -1;
  }
  globals::regionFinder = new region(cl, hotThresh);
  globals::cBB = new basicBlock(entry_p);
  s->pc = entry_p;
  mkMonitorVectors(s);
  initCapstone();
  
  estart = timestamp();
  if(setjmp(jenv) > 0) {
    std::cerr << globals::binaryName << ": returning from longjmp\n";
  }

  if(not(globals::enableCFG)) {
    if(globals::isMipsEL) {
      while(s->brk==0 and s->icnt < max_icnt) {
	interpretEL(s);
      }
    }
    else {
      while(s->brk==0 and s->icnt < max_icnt) {
	interpret(s);
      }
    }
  }
  else {
    if(globals::isMipsEL) {
      while(s->brk==0) {
	if(not(globals::cBB->executeJIT(s)))
	  interpretAndBuildCFGEL(s);
      }
    }
    else {
      while(s->brk==0) {
	if(not(globals::cBB->executeJIT(s)))
	  interpretAndBuildCFG(s);
      }
    }
  }
  estop = timestamp();
  double runtime = (estop-estart);
  struct rusage usage;
  uint64_t dupIns = 0;
  getrusage(RUSAGE_SELF,&usage);  
  for(auto &p : basicBlock::insInBBCnt) {
    if(p.second > 1) {
      dupIns++;
    }
  }
  
  std::cerr << KGRN << globals::binaryName << " statistics\n"
	    << "\t"
	    << runtime << " sec, "
	    << s->icnt
	    << " ins executed"
	    << " (" << regionCFG::icnt << " cfg), "
	    << std::round(s->icnt / (runtime*1e6))
	    << " megains/sec" << "\n"
	    << "\t" << 100.0*(static_cast<double>(regionCFG::icnt)/s->icnt)
	    << "% of instructions executed in CFG JIT\n"
	    << "\t" << 100.0*(static_cast<double>(s->icnt - regionCFG::icnt)/s->icnt)
	    << "% of instructions executed in the interpreter\n"
	    << "\t"
	    << "regionCFGs size="
	    << regionCFG::regionCFGs.size()
	    << ", compile called = "
	    << globals::nCfgCompiles
	    << " times\n"
	    << "\t"
	    << basicBlock::numBBs() << " basic blocks, "
	    << basicBlock::numStaticInsns() << " static instructions, "
	    << dupIns << " duplicated instructions\n"
	    << "\tcode invoked = "
	    << regionCFG::iters
	    << " times, "
	    << (static_cast<double>(regionCFG::icnt)/ regionCFG::iters)
	    << " insns per invocation on average\n"
	    << "\t" << usage
	    << KNRM << "\n";
  
  if(globals::simPoints) {
    save_simpoints_data(simPointsFname);
  }

  
  if(report) {
    debugSymDB::init(filename.c_str());
    std::vector<execUnit*> eUnitVec;
    for(auto tbb : regionCFG::regionCFGs) {
      eUnitVec.push_back(tbb);
    }
    for(auto p : basicBlock::bbMap) {
      eUnitVec.push_back(p.second);
    }
    std::sort(eUnitVec.begin(), eUnitVec.end(), execUnit::execUnitSorter());
    
    std::string reportStr;
    for(size_t i = 0; i < eUnitVec.size(); i++) {
      eUnitVec[i]->report(reportStr, s->icnt);
    }
    std::string reportName = std::string("./") + filename + std::string("-report.txt");
    //std::string reportName = std::string("/home/dsheffie/mips_regression/") + filename + std::string("-report.txt");
    std::fstream freport(reportName, std::fstream::out);
    if(freport.is_open()) {
      freport << reportStr;
      freport.close();
    }
  }

  basicBlock::dropAllBBs();
  delete globals::regionFinder;
  
  if(hash) {
    std::cerr << "crc32=" << std::hex
	      << crc32(mem, 1LU<<32)<<std::dec
	      << "\n";
  }
      

  munmap(mempt, 1UL<<32);

  if(sysArgv) {
    for(int i = 0; i < sysArgc; i++) {
      delete [] sysArgv[i];
    }
    delete [] sysArgv;
  }

  delete s;
  llvm::llvm_shutdown();
  debugSymDB::release();
  stopCapstone(); 
  return 0;
}

int buildArgcArgv(const char *filename, const std::string &sysArgs, char **&argv){
  int cnt = 0;
  std::vector<std::string> args;
  char **largs = 0;
  args.push_back(std::string(filename));

  char *ptr = nullptr;
  char *c_str = strdup(sysArgs.c_str());
  if(sysArgs.size() != 0)
    ptr = strtok(c_str, " ");

  while(ptr && (cnt<MARGS)) {
    args.push_back(std::string(ptr));
    ptr = strtok(nullptr, " ");
    cnt++;
  }
  largs = new char*[args.size()];
  for(size_t i = 0; i < args.size(); i++)
    {
      std::string s = args[i];
      size_t l = strlen(s.c_str());
      largs[i] = new char[l+1];
      memset(largs[i],0,sizeof(char)*(l+1));
      memcpy(largs[i],s.c_str(),sizeof(char)*l);
    }
  argv = largs;
  free(c_str);
  return (int)args.size();
}

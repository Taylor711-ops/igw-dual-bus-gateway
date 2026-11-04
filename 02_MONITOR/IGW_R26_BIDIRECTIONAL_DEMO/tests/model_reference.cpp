#include "../src/npu_model.h"
#include "../npu/src/reference.hpp"
#include <iostream>
int main(){
 igwref::verify_export();
 for(unsigned n=0;n<igwmodel::Count;n++){
  unsigned x[16],y[3],clipped;nm_reference(igwmodel::Features[n],x,y,&clipped);
  for(unsigned i=0;i<16;i++)if(x[i]!=igwmodel::GoldenInput[n][i])return 1;
  for(unsigned i=0;i<3;i++)if(y[i]!=igwmodel::GoldenLogits[n][i])return 2;
  if(clipped!=(unsigned)igwmodel::InputClippedDims[n])return 3;
 }
 std::cout<<"PASS frozen C client vs C++ reference and 150 golden vectors; no hardware inference\n";
}

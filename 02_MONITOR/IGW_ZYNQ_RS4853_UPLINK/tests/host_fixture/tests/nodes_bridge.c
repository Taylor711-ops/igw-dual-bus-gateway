#include "igw_nodes.h"
static struct igw_nodes nodes;
void nodes_reset(void){igw_nodes_init(&nodes);nodes.regs[2][8]=0x0301;}
unsigned nodes_get(unsigned unit,unsigned address){int i=igw_node_index(unit);return i>=0&&address<32?nodes.regs[i][address]:0;}
unsigned nodes_can(unsigned id,const uint8_t *p,uint8_t *out,uint64_t now){return (unsigned)igw_nodes_can(&nodes,id,p,out,now);}
unsigned nodes_rtu(const uint8_t *p,unsigned n,uint8_t *out){return (unsigned)igw_nodes_rtu(&nodes,p,n,out,0);}

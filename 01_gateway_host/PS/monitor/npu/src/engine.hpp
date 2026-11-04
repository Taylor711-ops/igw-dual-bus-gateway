#pragma once
#include "reference.hpp"
#include <armnn/ArmNN.hpp>
#include <armnn/IStrategy.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
namespace igwengine {
armnn::TensorInfo tensor(unsigned n,double scale,int zero) {
    return armnn::TensorInfo(armnn::TensorShape({1,n}),armnn::DataType::QAsymmU8,static_cast<float>(scale),zero);
}
armnn::IConnectableLayer* add_fc(armnn::INetwork& net,armnn::IConnectableLayer& previous,
    unsigned in,unsigned out,const std::uint8_t* weights,const std::int32_t* bias,
    double input_scale,double weight_scale,double output_scale,int output_zero,const char* name) {
    armnn::TensorInfo wi(armnn::TensorShape({out,in}),armnn::DataType::QAsymmU8,static_cast<float>(weight_scale),128,true);
    armnn::TensorInfo bi(armnn::TensorShape({out}),armnn::DataType::Signed32,static_cast<float>(input_scale*weight_scale),0,true);
    auto* w=net.AddConstantLayer(armnn::ConstTensor(wi,weights),(std::string(name)+"_weights").c_str());
    auto* b=net.AddConstantLayer(armnn::ConstTensor(bi,bias),(std::string(name)+"_bias").c_str());
    armnn::FullyConnectedDescriptor d;d.m_BiasEnabled=true;d.m_TransposeWeightMatrix=true;d.m_ConstantWeights=true;
    auto* fc=net.AddFullyConnectedLayer(d,name);
    w->GetOutputSlot(0).SetTensorInfo(wi);b->GetOutputSlot(0).SetTensorInfo(bi);
    previous.GetOutputSlot(0).Connect(fc->GetInputSlot(0));
    w->GetOutputSlot(0).Connect(fc->GetInputSlot(1));b->GetOutputSlot(0).Connect(fc->GetInputSlot(2));
    fc->GetOutputSlot(0).SetTensorInfo(tensor(out,output_scale,output_zero));
    return fc;
}
class Inspect final:public armnn::IStrategy {
public:
    unsigned hard=0,fc=0,fused=0,activation=0;
    void ExecuteStrategy(const armnn::IConnectableLayer* l,const armnn::BaseDescriptor& descriptor,
                         const std::vector<armnn::ConstTensor>&,const char* name,
                         armnn::LayerBindingId) override {
        std::cout<<"OPT_LAYER="<<(name?name:"")<<" TYPE="<<static_cast<int>(l->GetType())<<std::endl;
        if(l->GetType()==armnn::LayerType::ALHardNPU) {
            ++hard;const auto& d=static_cast<const armnn::ALHardNPUDescriptor&>(descriptor);
            if(d.m_IsFCMode)++fc;
            if(d.m_HasAct && (d.m_ActType==armnn::ALHardNPUDescriptor::ActType::Relu ||
                             d.m_ActDesc.m_Function==armnn::ActivationFunction::ReLu))++fused;
            const auto& t=l->GetOutputSlot(0).GetTensorInfo();
            std::cout<<"HARD_DESCRIPTOR layer="<<(name?name:"")<<" has_act="<<d.m_HasAct
                     <<" act_type="<<static_cast<int>(d.m_ActType)<<" output_scale="<<t.GetQuantizationScale()
                     <<" output_zero="<<t.GetQuantizationOffset()<<std::endl;
        }
        if(l->GetType()==armnn::LayerType::Activation)++activation;
    }
};
void check_tensor(const armnn::TensorInfo& t,unsigned n,double scale,int zero) {
    if(t.GetDataType()!=armnn::DataType::QAsymmU8 || t.GetShape()!=armnn::TensorShape({1,n}) ||
       t.GetQuantizationScale()!=static_cast<float>(scale) || t.GetQuantizationOffset()!=zero)
        throw std::runtime_error("runtime_tensor_contract_changed");
}
class Engine {
    armnn::IRuntimePtr runtime;
    armnn::NetworkId id=0;
    armnn::TensorInfo in_info,out_info;
    bool loaded=false;
public:
    explicit Engine(bool full):runtime(armnn::IRuntime::Create(armnn::IRuntime::CreationOptions())) {
        bool found=false;
        for(const auto& backend:runtime->GetDeviceSpec().GetSupportedBackends()) {
            std::cout<<"REGISTERED_BACKEND="<<backend<<std::endl;
            if(backend==armnn::BackendId("Alnpu"))found=true;
        }
        if(!found)throw std::runtime_error("Alnpu_not_registered");
        auto net=armnn::INetwork::Create();auto* input=net->AddInputLayer(0,"features16");
        input->GetOutputSlot(0).SetTensorInfo(tensor(16,igwmodel::InputScale,128));
        auto* fc1=add_fc(*net,*input,16,8,&igwmodel::Weight1[0][0],igwmodel::Bias1,
                        igwmodel::InputScale,igwmodel::Weight1Scale,igwmodel::HiddenScale,0,"trained_fc16_8");
        armnn::ActivationDescriptor ad;ad.m_Function=armnn::ActivationFunction::ReLu;
        auto* act=net->AddActivationLayer(ad,"trained_hidden_relu");
        fc1->GetOutputSlot(0).Connect(act->GetInputSlot(0));
        act->GetOutputSlot(0).SetTensorInfo(tensor(8,igwmodel::HiddenScale,0));
        auto* last=full?add_fc(*net,*act,8,3,&igwmodel::Weight2[0][0],igwmodel::Bias2,
                   igwmodel::HiddenScale,igwmodel::Weight2Scale,igwmodel::OutputScale,128,"trained_fc8_3"):act;
        auto* output=net->AddOutputLayer(0,full?"logits3":"hidden8");
        last->GetOutputSlot(0).Connect(output->GetInputSlot(0));
        std::vector<std::string> messages;
        auto optimized=armnn::Optimize(*net,{armnn::BackendId("Alnpu")},runtime->GetDeviceSpec(),
                    armnn::OptimizerOptionsOpaque(),armnn::Optional<std::vector<std::string>&>(messages));
        for(const auto& m:messages)std::cout<<"OPT_MESSAGE="<<m<<std::endl;
        if(!optimized)throw std::runtime_error("strict_Alnpu_optimization_failed");
        Inspect check;optimized->ExecuteStrategy(check);
        unsigned count=full?2:1;
        std::cout<<"GRAPH_MODE="<<(full?"classifier":"hidden")<<" ALHARDNPU="<<check.hard
                 <<" FC="<<check.fc<<" FUSED_RELU="<<check.fused<<" SEPARATE_ACTIVATION="<<check.activation<<std::endl;
        if(check.hard!=count || check.fc!=count || check.fused+check.activation>1)
            throw std::runtime_error("optimized_graph_contract_changed");
        std::ofstream dot(full?"classifier.dot":"hidden.dot");
        if(!dot)throw std::runtime_error("cannot_open_graph_file");
        optimized->SerializeToDot(dot);
        std::string error;
        if(runtime->LoadNetwork(id,std::move(optimized),error)!=armnn::Status::Success)
            throw std::runtime_error("load_failed:"+error);
        loaded=true;
        in_info=runtime->GetInputTensorInfo(id,0);out_info=runtime->GetOutputTensorInfo(id,0);
        check_tensor(in_info,16,igwmodel::InputScale,128);
        check_tensor(out_info,full?3:8,full?igwmodel::OutputScale:igwmodel::HiddenScale,full?128:0);
    }
    ~Engine() {if(loaded)runtime->UnloadNetwork(id);}
    std::vector<std::uint8_t> infer(const igwref::Input& x,long long& us) {
        std::vector<std::uint8_t> actual(out_info.GetNumElements(),0);
        auto constant=in_info;constant.SetConstant();
        auto start=std::chrono::steady_clock::now();
        auto status=runtime->EnqueueWorkload(id,{{0,armnn::ConstTensor(constant,x.data())}},
                                            {{0,armnn::Tensor(out_info,actual.data())}});
        us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        if(status!=armnn::Status::Success)throw std::runtime_error("enqueue_failed");
        return actual;
    }
    void self_test(bool full) {
        unsigned max_diff=0;
        for(unsigned n=0;n<igwmodel::Count;++n) {
            auto x=igwref::preprocess(igwmodel::Features[n]);
            auto h=igwref::hidden(x);auto y=igwref::logits(h);
            long long us=0;auto actual=infer(x,us);
            for(unsigned i=0;i<actual.size();++i) {
                unsigned diff=static_cast<unsigned>(std::abs(int(actual[i])-int(full?y[i]:h[i])));
                max_diff=std::max(max_diff,diff);
                if(diff>unsigned(full?igwmodel::LogitTolerance[i]:1))throw std::runtime_error("startup_numeric_self_test_failed");
            }
            if(full) {
                igwref::Logits a={{actual[0],actual[1],actual[2]}};
                if(igwref::argmax(a)!=igwref::argmax(y))throw std::runtime_error("startup_class_disagreement");
            }
        }
        std::cout<<"SELF_TEST_MODE="<<(full?"classifier":"hidden")<<" CASES="<<igwmodel::Count
                 <<" MAX_DIFF="<<max_diff<<" RESULT=PASS"<<std::endl;
    }
};
}

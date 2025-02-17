#include "src/primihub/algorithm/falcon_lenet.h"

int partyNum;
AESObject *aes_indep;
AESObject *aes_next;
AESObject *aes_prev;
Precompute PrecomputeObject;
std::string train_data_A = "test/primihub/script/Falcon_Mnist/train_data_A";
std::string train_data_B = "test/primihub/script/Falcon_Mnist/train_data_B";
std::string train_data_C = "test/primihub/script/Falcon_Mnist/train_data_C";
std::string train_labels_A = "test/primihub/script/Falcon_Mnist/train_labels_A";
std::string train_labels_B = "test/primihub/script/Falcon_Mnist/train_labels_B";
std::string train_labels_C = "test/primihub/script/Falcon_Mnist/train_labels_C";

// For faster modular operations
extern smallType additionModPrime[PRIME_NUMBER][PRIME_NUMBER];
extern smallType subtractModPrime[PRIME_NUMBER][PRIME_NUMBER];
extern smallType multiplicationModPrime[PRIME_NUMBER][PRIME_NUMBER];

namespace primihub {
FalconLenetExecutor::FalconLenetExecutor(
    PartyConfig &config, std::shared_ptr<DatasetService> dataset_service)
    : AlgorithmBase(dataset_service) {
  this->algorithm_name_ = "Falcon_Lenet_Train";

  node_id_ = config.node_id;
  std::map<std::string, Node> node_map = config.node_map;
  auto iter = node_map.find(node_id_);
  if (iter == node_map.end()) {
    std::stringstream ss;
    ss << "Can't find node " << node_id_ << " in node map.";
    throw std::runtime_error(ss.str());
  }

  local_id_ = iter->second.vm(0).party_id();

  {
    const rpc::VirtualMachine &vm = iter->second.vm(0);
    const rpc::EndPoint &ep0 = vm.next();
    const rpc::EndPoint &ep1 = vm.prev();
    listen_addrs_.emplace_back(std::make_pair(ep0.ip(), ep0.port()));
    listen_addrs_.emplace_back(std::make_pair(ep1.ip(), ep1.port()));
    VLOG(3) << "Addr to listen: " << listen_addrs_[0].first << ":"
            << listen_addrs_[0].second << ", " << listen_addrs_[1].first << ":"
            << listen_addrs_[1].second << ".";
  }

  {
    const rpc::VirtualMachine &vm = iter->second.vm(1);
    const rpc::EndPoint &ep0 = vm.next();
    const rpc::EndPoint &ep1 = vm.prev();
    connect_addrs_.emplace_back(std::make_pair(ep0.ip(), ep0.port()));
    connect_addrs_.emplace_back(std::make_pair(ep1.ip(), ep1.port()));
    VLOG(3) << "Addr to connect: " << connect_addrs_[0].first << ":"
            << connect_addrs_[0].second << ", " << connect_addrs_[1].first
            << ":" << connect_addrs_[1].second << ".";
  }

  // Key when save model.
  std::stringstream ss;
  ss << config.job_id << "_" << config.task_id << "_party_" << local_id_
     << "_lr";
  model_name_ = ss.str();
}

int FalconLenetExecutor::loadParams(primihub::rpc::Task &task) {
  auto param_map = task.params().param_map();
  try {
    batch_size_ = param_map["BatchSize"].value_int32();
    num_iter_ = param_map["NumIters"].value_int32(); // iteration rounds
  } catch (std::exception &e) {
    LOG(ERROR) << "Failed to load params: " << e.what();
    return -1;
  }

  LOG(INFO) << "Training on MNIST using Lenet:\t BatchSize:" << batch_size_
            << ", Epoch  " << num_iter_ << ".";
  return 0;
}

int FalconLenetExecutor::loadDataset() { return 0; }

int FalconLenetExecutor::initPartyComm(void) {
  /****************************** AES SETUP and SYNC
   * ******************************/
  partyNum = local_id_;
  if (local_id_ == PARTY_A) {
    aes_indep = new AESObject("key/falcon/keyA");
    aes_next = new AESObject("key/falcon/keyAB");
    aes_prev = new AESObject("key/falcon/keyAC");
  }

  if (local_id_ == PARTY_B) {
    aes_indep = new AESObject("key/falcon/keyB");
    aes_next = new AESObject("key/falcon/keyBC");
    aes_prev = new AESObject("key/falcon/keyAB");
  }

  if (local_id_ == PARTY_C) {
    aes_indep = new AESObject("key/falcon/keyC");
    aes_next = new AESObject("key/falcon/keyAC");
    aes_prev = new AESObject("key/falcon/keyBC");
  }

  initializeCommunication(listen_addrs_, connect_addrs_);
  synchronize(2000000);

  return 0;
}

int FalconLenetExecutor::finishPartyComm(void) {

  /****************************** CLEAN-UP ******************************/

  delete aes_indep;
  delete aes_next;
  delete aes_prev;
  delete config_lenet;
  delete net_lenet;
  deleteObjects();
  return 0;
}

int FalconLenetExecutor::execute() {
  /****************************** PREPROCESSING ******************************/
  /*for faster module multiply and addition*/

  for (int i = 0; i < PRIME_NUMBER; ++i)
    for (int j = 0; j < PRIME_NUMBER; ++j) {
      additionModPrime[i][j] = ((i + j) % PRIME_NUMBER);
      subtractModPrime[i][j] = ((PRIME_NUMBER + i - j) % PRIME_NUMBER);
      multiplicationModPrime[i][j] = ((i * j) % PRIME_NUMBER);
    }
  


  /****************************** SELECT NETWORK ******************************/
  std::string network = "LeNet";
  std::string dataset = "MNIST";
  std::string security = "Semi-honest";

  config_lenet = new NeuralNetConfig(1);
  selectNetwork(network, dataset, security, config_lenet);
  config_lenet->checkNetwork();
  net_lenet = new NeuralNetwork(config_lenet);

  start_m();
  LOG(INFO) << "Training on MNIST using Lenet";
  
  num_iter_ = 1;
  for (int i = 0; i < 1; ++i) {
    readMiniBatch(net_lenet, "TRAINING");
    net_lenet->forward();
    net_lenet->backward();
  }

  end_m(network);

  LOG(INFO) << "----------------------------------------------" << endl;
  LOG(INFO) << "Run details: " << NUM_OF_PARTIES << "PC (P" << partyNum << "), "
       << NUM_ITERATIONS << " iterations, batch size " << MINI_BATCH_SIZE
       << endl
       << "Running " << security << " " << network << " on " << dataset
       << " dataset" << endl;
  LOG(INFO) << "----------------------------------------------" << endl << endl;

  LOG(INFO) << "Party " << local_id_ << " train finish.";
  return 0;
}
} // namespace primihub

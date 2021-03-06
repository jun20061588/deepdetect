/**
 * DeepDetect
 * Copyright (c) 2019-2020 Jolibrain
 * Authors: Louis Jean <ljean@etud.insa-toulouse.fr>
 *          Guillaume Infantes <guillaume.infantes@jolibrain.com>
 *
 * This file is part of deepdetect.
 *
 * deepdetect is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * deepdetect is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with deepdetect.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TORCHLIB_H
#define TORCHLIB_H

#include <random>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <torch/torch.h>
#pragma GCC diagnostic pop

#include "apidata.h"
#include "mllibstrategy.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "caffe.pb.h"
#pragma GCC diagnostic pop

#include "torchmodel.h"
#include "torchinputconns.h"
#include "torchgraphbackend.h"
#include "native/native_net.h"

namespace dd
{
  // TODO TorchModule may be merged with TorchGraph in the future
  class TorchModule
  {
  public:
    TorchModule();

    c10::IValue forward(std::vector<c10::IValue> source);
    c10::IValue extract(std::vector<c10::IValue> source,
                        std::string extract_layer);

    bool extractable(std::string extract_layer) const;
    std::vector<std::string> extractable_layers() const;

    void freeze_traced(bool freeze);

    /** Add linear model at the end of module. Automatically detects size of
     * the last layer thanks to the provided example output.*/
    void setup_classification(int nclasses,
                              std::vector<c10::IValue> input_example);

    std::vector<torch::Tensor> parameters();

    /** Save traced module to checkpoint-[name].pt, and custom parts weights
     * to checkpoint-[name].ptw */
    // (Actually only _classif is saved in the .ptw)
    void save_checkpoint(TorchModel &model, const std::string &name);

    /** Load traced module from .pt and custom parts weights from .ptw */
    void load(TorchModel &model);

    void eval();
    void train();

    void free();

    template <class TInputConnectorStrategy>
    void post_transform(const std::string tmpl, const APIData &template_params,
                        const TInputConnectorStrategy &inputc,
                        const TorchModel &tmodel, const torch::Device &device);

    template <class TInputConnectorStrategy>
    void post_transform_train(const std::string tmpl,
                              const APIData &template_params,
                              const TInputConnectorStrategy &inputc,
                              const TorchModel &tmodel,
                              const torch::Device &device);

    template <class TInputConnectorStrategy>
    void post_transform_predict(const std::string tmpl,
                                const APIData &template_params,
                                const TInputConnectorStrategy &inputc,
                                const TorchModel &tmodel,
                                const torch::Device &device,
                                const APIData &ad);

    /**
     * \brief see torch::module::to
     * @param device cpu / gpu
     * @param non_blocking
     */
    void to(torch::Device device);

    /**
     * \brief see torch::module::to
     * @param dtype : torch::kFloat32 or torch::kFloat64
     * @param non_blocking
     */
    void to(torch::Dtype dtype);

    /**
     * \brief see torch::module::to
     * @param device cpu / gpu
     * @param dtype : torch::kFloat32 or torch::kFloat64
     * @param non_blocking
     */
    void to(torch::Device device, torch::Dtype dtype);

  public:
    std::shared_ptr<torch::jit::script::Module> _traced;
    std::shared_ptr<TorchGraphBackend>
        _graph; /**< graph module : torchgraphbackend has same interface as
                   torch::module */
    std::shared_ptr<NativeModule>
        _native; /**< native module : directly written in C++ */

    torch::nn::Linear _classif = nullptr;

    torch::Device _device;
    int _classif_in = 0; /**<id of the input of the classification layer */
    bool _hidden_states = false; /**< Take BERT hidden states as input. */

    bool _require_classif_layer = false;
    std::string
        _classif_layer_file; /** < if require_classif_layer == true, this is
                                the file where the weights are stored */
    unsigned int _nclasses = 0;

  private:
    bool _freeze_traced = false; /**< Freeze weights of the traced module */
  };

  template <class TInputConnectorStrategy, class TOutputConnectorStrategy,
            class TMLModel = TorchModel>
  class TorchLib : public MLLib<TInputConnectorStrategy,
                                TOutputConnectorStrategy, TMLModel>
  {
  public:
    TorchLib(const TorchModel &tmodel);
    TorchLib(TorchLib &&tl) noexcept;
    ~TorchLib();

    /*- from mllib -*/
    void init_mllib(const APIData &ad);

    void clear_mllib(const APIData &ad);

    int train(const APIData &ad, APIData &out);

    int predict(const APIData &ad, APIData &out);

    int test(const APIData &ad, TInputConnectorStrategy &inputc,
             TorchDataset &dataset, int batch_size, APIData &out);

  public:
    unsigned int _nclasses = 0;
    std::string _template;
    bool _finetuning = false;
    torch::Device _device = torch::Device("cpu");
    bool _masked_lm = false;
    bool _seq_training = false;
    bool _classification = false;
    bool _timeserie = false;
    std::string _loss = "";

    APIData _template_params;

    // models
    TorchModule _module;

    std::vector<std::string>
        _best_metrics;         /**< metric to use for saving best model */
    double _best_metric_value; /**< best metric value  */

  private:
    /**
     * \brief checks wether v1 is better than v2
     */
    bool is_better(double v1, double v2, std::string metric_name);

    /**
     * \brief generates a file containing best iteration so far
     */
    int64_t save_if_best(APIData &meas_out, int64_t elapsed_it,
                         torch::optim::Optimizer &optimizer,
                         int64_t best_to_remove);

    void snapshot(int64_t elapsed_it, torch::optim::Optimizer &optimizer);

    void remove_model(int64_t it);

    double unscale(double val, unsigned int k,
                   const TInputConnectorStrategy &inputc);
  };
}

#endif // TORCHLIB_H

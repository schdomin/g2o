// g2o - General Graph Optimization
// Copyright (C) 2011 R. Kuemmerle, G. Grisetti, W. Burgard
// 
// g2o is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// g2o is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "sparse_optimizer.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <iterator>
#include <cassert>
#include <algorithm>

#include "estimate_propagator.h"
#include "optimization_algorithm.h"
#include "batch_stats.h"
#include "hyper_graph_action.h"
#include "g2o/stuff/timeutil.h"
#include "g2o/stuff/macros.h"
#include "g2o/config.h"

namespace g2o{
  using namespace std;


  SparseOptimizer::SparseOptimizer() :
    _forceStopFlag(0), _verbose(false), _algorithm(0), _statistics(0)
  {
    _graphActions.resize(AT_NUM_ELEMENTS);
  }

  SparseOptimizer::~SparseOptimizer(){
    delete _algorithm;
    delete[] _statistics;
  }

  void SparseOptimizer::computeActiveErrors()
  {
    // call the callbacks in case there is something registered
    HyperGraphActionSet& actions = _graphActions[AT_COMPUTEACTIVERROR];
    if (actions.size() > 0) {
      for (HyperGraphActionSet::iterator it = actions.begin(); it != actions.end(); ++it)
        (*(*it))(this);
    }

    for (EdgeContainer::const_iterator it = _activeEdges.begin(); it != _activeEdges.end(); it++) {
      OptimizableGraph::Edge* e = *it;
      e->computeError();
      if (e->robustKernel()) { 
        e->robustifyError();
      }
    }
  }

  double SparseOptimizer::activeChi2( ) const
  {
    double chi = 0.0;
    for (EdgeContainer::const_iterator it = _activeEdges.begin(); it != _activeEdges.end(); it++) {
      const OptimizableGraph::Edge* e = *it;
      chi += e->chi2();
    }
    return chi;
  }

  OptimizableGraph::Vertex* SparseOptimizer::findGauge(){
    if (vertices().empty())
      return 0;

    int maxDim=0;
    for (HyperGraph::VertexIDMap::iterator it=vertices().begin(); it!=vertices().end(); it++){
      OptimizableGraph::Vertex* v=static_cast<OptimizableGraph::Vertex*>(it->second); 
      maxDim=std::max(maxDim,v->dimension());
    }
    
    OptimizableGraph::Vertex* rut=0;
    for (HyperGraph::VertexIDMap::iterator it=vertices().begin(); it!=vertices().end(); it++){
      OptimizableGraph::Vertex* v=static_cast<OptimizableGraph::Vertex*>(it->second);
      if (v->dimension()==maxDim){
        rut=v;
        break;
      }
    }
    return rut;
  }

  bool SparseOptimizer::gaugeFreedom()
  {
    if (vertices().empty())
      return false;

    int maxDim=0;
    for (HyperGraph::VertexIDMap::iterator it=vertices().begin(); it!=vertices().end(); it++){
      OptimizableGraph::Vertex* v=static_cast<OptimizableGraph::Vertex*>(it->second); 
      maxDim = std::max(maxDim,v->dimension());
    }

    for (HyperGraph::VertexIDMap::iterator it=vertices().begin(); it!=vertices().end(); it++){
      OptimizableGraph::Vertex* v=static_cast<OptimizableGraph::Vertex*>(it->second);
      if (v->dimension() == maxDim) {
        // test for fixed vertex
        if (v->fixed()) {
          return false;
        }
        // test for full dimension prior
        for (HyperGraph::EdgeSet::const_iterator eit = v->edges().begin(); eit != v->edges().end(); ++eit) {
          OptimizableGraph::Edge* e = static_cast<OptimizableGraph::Edge*>(*eit);
          if (e->vertices().size() == 1 && e->dimension() == maxDim)
            return false;
        }
      }
    }
    return true;
  }

  bool SparseOptimizer::buildIndexMapping(SparseOptimizer::VertexContainer& vlist){
    if (! vlist.size()){
      _ivMap.clear();
      return false;
    }

    _ivMap.resize(vlist.size());
    size_t i = 0;
    for (int k=0; k<2; k++)
      for (VertexContainer::iterator it=vlist.begin(); it!=vlist.end(); it++){
      OptimizableGraph::Vertex* v = *it;
      if (! v->fixed()){
        if (static_cast<int>(v->marginalized()) == k){
          v->setTempIndex(i);
          _ivMap[i]=v;
          i++;
        }
      }
      else {
        v->setTempIndex(-1);
      }
    }
    _ivMap.resize(i);
    return true;
  }

  void SparseOptimizer::clearIndexMapping(){
    for (size_t i=0; i<_ivMap.size(); i++){
      _ivMap[i]->setTempIndex(-1);
      _ivMap[i]=0;
    }
  }

  bool SparseOptimizer::initializeOptimization(int level){
    HyperGraph::VertexSet vset;
    for (VertexIDMap::iterator it=vertices().begin(); it!=vertices().end(); it++)
      vset.insert(it->second);
    return initializeOptimization(vset,level);
  }

  bool SparseOptimizer::initializeOptimization(HyperGraph::VertexSet& vset, int level){
    clearIndexMapping();
    _activeVertices.clear();
    _activeVertices.reserve(vset.size());
    _activeEdges.clear();
    set<Edge*> auxEdgeSet; // temporary structure to avoid duplicates
    for (HyperGraph::VertexSet::iterator it=vset.begin(); it!=vset.end(); it++){
      OptimizableGraph::Vertex* v= (OptimizableGraph::Vertex*) *it;
      const OptimizableGraph::EdgeSet& vEdges=v->edges();
      // count if there are edges in that level. If not remove from the pool
      int levelEdges=0;
      for (OptimizableGraph::EdgeSet::const_iterator it=vEdges.begin(); it!=vEdges.end(); ++it){
        OptimizableGraph::Edge* e=reinterpret_cast<OptimizableGraph::Edge*>(*it);
        if (level < 0 || e->level() == level) {

          bool allVerticesOK = true;
          for (vector<HyperGraph::Vertex*>::const_iterator vit = e->vertices().begin(); vit != e->vertices().end(); ++vit) {
            if (vset.find(*vit) == vset.end()) {
              allVerticesOK = false;
              break;
            }
          }
          if (allVerticesOK) {
            auxEdgeSet.insert(reinterpret_cast<OptimizableGraph::Edge*>(*it));
            levelEdges++;
          }

        }
      }
      if (levelEdges){
        _activeVertices.push_back(v);

        // test for NANs in the current estimate if we are debugging
#      ifndef NDEBUG
        int estimateDim = v->estimateDimension();
        if (estimateDim > 0) {
#        ifdef _MSC_VER
          double* estimateData = new double[];
#        else
          double estimateData[estimateDim];
#        endif
          if (v->getEstimateData(estimateData) == true) {
            for (int k = 0; k < estimateDim; ++k) {
              if (g2o_isnan(estimateData[k])) {
                cerr << __PRETTY_FUNCTION__ << ": Vertex " << v->id() << " contains a nan entry at index " << k << endl;
              }
            }
          }
#        ifdef _MSC_VER
          delete[] estimateData;
#        endif
        }
#      endif

      }
    }

    _activeEdges.reserve(auxEdgeSet.size());
    for (set<Edge*>::iterator it = auxEdgeSet.begin(); it != auxEdgeSet.end(); ++it)
      _activeEdges.push_back(*it);

    sortVectorContainers();
    return buildIndexMapping(_activeVertices);
  }

  bool SparseOptimizer::initializeOptimization(HyperGraph::EdgeSet& eset){
    clearIndexMapping();
    _activeVertices.clear();
    _activeEdges.clear();
    _activeEdges.reserve(eset.size());
    set<Vertex*> auxVertexSet; // temporary structure to avoid duplicates
    for (HyperGraph::EdgeSet::iterator it=eset.begin(); it!=eset.end(); it++){
      OptimizableGraph::Edge* e=(OptimizableGraph::Edge*)(*it);
      for (vector<HyperGraph::Vertex*>::const_iterator vit = e->vertices().begin(); vit != e->vertices().end(); ++vit) {
        auxVertexSet.insert(static_cast<OptimizableGraph::Vertex*>(*vit));
      }
      _activeEdges.push_back(reinterpret_cast<OptimizableGraph::Edge*>(*it));
    }

    _activeVertices.reserve(auxVertexSet.size());
    for (set<Vertex*>::iterator it = auxVertexSet.begin(); it != auxVertexSet.end(); ++it)
      _activeVertices.push_back(*it);

    sortVectorContainers();
    return buildIndexMapping(_activeVertices);
  }

  void SparseOptimizer::computeInitialGuess()
  {
    OptimizableGraph::VertexSet emptySet;
    std::set<Vertex*> backupVertices;
    HyperGraph::VertexSet fixedVertices; // these are the root nodes where to start the initialization
    for (EdgeContainer::iterator it = _activeEdges.begin(); it != _activeEdges.end(); ++it) {
      OptimizableGraph::Edge* e = *it;
      for (size_t i = 0; i < e->vertices().size(); ++i) {
        OptimizableGraph::Vertex* v = static_cast<OptimizableGraph::Vertex*>(e->vertex(i));
        if (v->fixed())
          fixedVertices.insert(v);
        else { // check for having a prior which is able to fully initialize a vertex
          for (EdgeSet::const_iterator vedgeIt = v->edges().begin(); vedgeIt != v->edges().end(); ++vedgeIt) {
            OptimizableGraph::Edge* vedge = static_cast<OptimizableGraph::Edge*>(*vedgeIt);
            if (vedge->vertices().size() == 1 && vedge->initialEstimatePossible(emptySet, v) > 0.) {
              //cerr << "Initialize with prior for " << v->id() << endl;
              vedge->initialEstimate(emptySet, v);
              fixedVertices.insert(v);
            }
          }
        }
        if (v->tempIndex() == -1) {
          std::set<Vertex*>::const_iterator foundIt = backupVertices.find(v);
          if (foundIt == backupVertices.end()) {
            v->push();
            backupVertices.insert(v);
          }
        }
      }
    }

    EstimatePropagator estimatePropagator(this);
    EstimatePropagator::PropagateCost costFunction(this);
    estimatePropagator.propagate(fixedVertices, costFunction);

    // restoring the vertices that should not be initialized
    for (std::set<Vertex*>::iterator it = backupVertices.begin(); it != backupVertices.end(); ++it) {
      Vertex* v = *it;
      v->pop();
    }
    if (verbose()) {
      computeActiveErrors();
      cerr << "iteration= -1\t chi2= " << activeChi2()
          << "\t time= 0.0"
          << "\t cumTime= 0.0"
          << "\t (using initial guess from spanning tree)" << endl;
    }
  }

  int SparseOptimizer::optimize(int iterations, bool online)
  {
    int cjIterations=0;
    double cumTime=0;
    bool ok=true;

    ok = _algorithm->init(online);
    if (! ok) {
      cerr << __PRETTY_FUNCTION__ << " Error while initializing" << endl;
      return -1;
    }

    for (int i=0; i<iterations && ! terminate() && ok; i++){
      preIteration(i);

      G2OBatchStatistics* cstat = _statistics ? &(_statistics[i]) : 0;
      globalStats = cstat;
      if (cstat) {
        cstat->iteration = i;
        cstat->numEdges =  _activeEdges.size();
        cstat->numVertices = _activeVertices.size();
      }

      double ts =  get_time();

      ok = _algorithm->solve(i, online) == OptimizationAlgorithm::OK;

      bool errorComputed = false;
      if (cstat) {
        computeActiveErrors();
        errorComputed = true;
        cstat->chi2 = activeChi2();
        cstat->timeIteration = get_time()-ts;
      }

      if (verbose()){
        double dts = get_time()-ts;
        cumTime += dts;
        if (! errorComputed)
          computeActiveErrors();
        cerr << "iteration= " << i
          << "\t chi2= " << FIXED(activeChi2())
          << "\t time= " << dts
          << "\t cumTime= " << cumTime
          << "\t edges= " << _activeEdges.size();
        _algorithm->printVerbose(cerr);
        cerr << endl;
      }
      ++cjIterations; 
      postIteration(i);
    }
    if (! ok)
      return 0;
    return cjIterations;
  }

  void SparseOptimizer::linearizeSystem()
  {
#   ifdef G2O_OPENMP
#   pragma omp parallel for default (shared) if (_activeEdges.size() > 50)
#   endif
    for (size_t k = 0; k < _activeEdges.size(); ++k) {
      OptimizableGraph::Edge* e = _activeEdges[k];
      e->linearizeOplus(); // jacobian of the nodes' oplus (manifold)
    }
  }

  void SparseOptimizer::update(const double* update)
  {
#ifndef NDEBUG
    size_t nan_idx = 0;
#endif
    // update the graph by calling oplus on the vertices
    for (size_t i=0; i < _ivMap.size(); ++i) {
      OptimizableGraph::Vertex* v= _ivMap[i];
#ifndef NDEBUG
      for (int k = 0; k < v->dimension(); k++) {
        if (g2o_isnan(update[k])) {
          cerr << __PRETTY_FUNCTION__ << ": Update contains a nan at " << nan_idx << endl;
        }
        nan_idx++;
      }
#endif
      v->oplus(update);
      update += v->dimension();
    }
  }

  bool SparseOptimizer::updateInitialization(HyperGraph::VertexSet& vset, HyperGraph::EdgeSet& eset)
  {
    std::vector<HyperGraph::Vertex*> newVertices;
    newVertices.reserve(vset.size());
    _activeVertices.reserve(_activeVertices.size() + vset.size());
    //for (HyperGraph::VertexSet::iterator it = vset.begin(); it != vset.end(); ++it)
      //_activeVertices.push_back(static_cast<OptimizableGraph::Vertex*>(*it));
    _activeEdges.reserve(_activeEdges.size() + eset.size());
    for (HyperGraph::EdgeSet::iterator it = eset.begin(); it != eset.end(); ++it)
      _activeEdges.push_back(static_cast<OptimizableGraph::Edge*>(*it));
    
    // update the index mapping
    size_t next = _ivMap.size();
    for (HyperGraph::VertexSet::iterator it = vset.begin(); it != vset.end(); ++it) {
      OptimizableGraph::Vertex* v=static_cast<OptimizableGraph::Vertex*>(*it);
      if (! v->fixed()){
        if (! v->marginalized()){
          v->setTempIndex(next);
          _ivMap.push_back(v);
          newVertices.push_back(v);
          _activeVertices.push_back(v);
          next++;
        } 
        else // not supported right now
          abort();
      }
      else {
        v->setTempIndex(-1);
      }
    }

    //if (newVertices.size() != vset.size())
    //cerr << __PRETTY_FUNCTION__ << ": something went wrong " << PVAR(vset.size()) << " " << PVAR(newVertices.size()) << endl;
    return _algorithm->updateStructure(newVertices, eset);
  }

  void SparseOptimizer::sortVectorContainers()
  {
    // sort vector structures to get deterministic ordering based on IDs
    sort(_activeVertices.begin(), _activeVertices.end(), VertexIDCompare());
    sort(_activeEdges.begin(), _activeEdges.end(), EdgeIDCompare());
  }

  void SparseOptimizer::clear() {
    _ivMap.clear();
    _activeVertices.clear();
    _activeEdges.clear();
    HyperGraph::clear();
  }

  SparseOptimizer::VertexContainer::const_iterator SparseOptimizer::findActiveVertex(OptimizableGraph::Vertex* v) const
  {
    VertexContainer::const_iterator lower = lower_bound(_activeVertices.begin(), _activeVertices.end(), v, VertexIDCompare());
    if (lower == _activeVertices.end())
      return _activeVertices.end();
    if ((*lower) == v)
      return lower;
    return _activeVertices.end();
  }

  SparseOptimizer::EdgeContainer::const_iterator SparseOptimizer::findActiveEdge(OptimizableGraph::Edge* e) const
  {
    EdgeContainer::const_iterator lower = lower_bound(_activeEdges.begin(), _activeEdges.end(), e, EdgeIDCompare());
    if (lower == _activeEdges.end())
      return _activeEdges.end();
    if ((*lower) == e)
      return lower;
    return _activeEdges.end();
  }

  void SparseOptimizer::push(SparseOptimizer::VertexContainer& vlist)
  {
    for (VertexContainer::iterator it = vlist.begin(); it != vlist.end(); ++it)
      (*it)->push();
  }

  void SparseOptimizer::pop(SparseOptimizer::VertexContainer& vlist)
  {
    for (VertexContainer::iterator it = vlist.begin(); it != vlist.end(); ++it)
      (*it)->pop();
  }

  void SparseOptimizer::push(HyperGraph::VertexSet& vlist)
  {
    for (HyperGraph::VertexSet::iterator it = vlist.begin(); it != vlist.end(); ++it) {
      OptimizableGraph::Vertex* v = dynamic_cast<OptimizableGraph::Vertex*>(*it);
      if (v)
	v->push();
      else 
	cerr << __FUNCTION__ << ": FATAL PUSH SET" << endl;
    }
  }

  void SparseOptimizer::pop(HyperGraph::VertexSet& vlist)
  {
    for (HyperGraph::VertexSet::iterator it = vlist.begin(); it != vlist.end(); ++it){
      OptimizableGraph::Vertex* v = dynamic_cast<OptimizableGraph::Vertex*> (*it);
      if (v)
	v->pop();
      else 
	cerr << __FUNCTION__ << ": FATAL POP SET" << endl;
    }
  }

  void SparseOptimizer::discardTop(SparseOptimizer::VertexContainer& vlist)
  {
    for (VertexContainer::iterator it = vlist.begin(); it != vlist.end(); ++it)
      (*it)->discardTop();
  }

  void SparseOptimizer::setVerbose(bool verbose)
  {
    _verbose = verbose;
  }

  void SparseOptimizer::setAlgorithm(OptimizationAlgorithm* algorithm)
  {
    if (_algorithm) // reset the optimizer for the formerly used solver
      _algorithm->setOptimizer(0);
    _algorithm = algorithm;
    if (_algorithm)
      _algorithm->setOptimizer(this);
  }

  bool SparseOptimizer::computeMarginals(SparseBlockMatrix<MatrixXd>& spinv, const std::vector<std::pair<int, int> >& blockIndices){
    return _algorithm->computeMarginals(spinv, blockIndices);
  }

  void SparseOptimizer::setForceStopFlag(bool* flag)
  {
    _forceStopFlag=flag;
  }

  bool SparseOptimizer::removeVertex(Vertex* v)
  {
    if (v->tempIndex() >= 0) {
      clearIndexMapping();
      _ivMap.clear();
    }
    return HyperGraph::removeVertex(v);
  }

  bool SparseOptimizer::addComputeErrorAction(HyperGraphAction* action)
  {
    std::pair<HyperGraphActionSet::iterator, bool> insertResult = _graphActions[AT_COMPUTEACTIVERROR].insert(action);
    return insertResult.second;
  }

  bool SparseOptimizer::removeComputeErrorAction(HyperGraphAction* action)
  {
    return _graphActions[AT_COMPUTEACTIVERROR].erase(action) > 0;
  }

  void SparseOptimizer::push()
  {
    push(_activeVertices);
  }

  void SparseOptimizer::pop()
  {
    pop(_activeVertices);
  }

  void SparseOptimizer::discardTop()
  {
    discardTop(_activeVertices);
  }

} // end namespace
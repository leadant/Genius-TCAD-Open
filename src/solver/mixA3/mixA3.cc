/********************************************************************************/
/*     888888    888888888   88     888  88888   888      888    88888888       */
/*   8       8   8           8 8     8     8      8        8    8               */
/*  8            8           8  8    8     8      8        8    8               */
/*  8            888888888   8   8   8     8      8        8     8888888        */
/*  8      8888  8           8    8  8     8      8        8            8       */
/*   8       8   8           8     8 8     8      8        8            8       */
/*     888888    888888888  888     88   88888     88888888     88888888        */
/*                                                                              */
/*       A Three-Dimensional General Purpose Semiconductor Simulator.           */
/*                                                                              */
/*                                                                              */
/*  Copyright (C) 2007-2008                                                     */
/*  Cogenda Pte Ltd                                                             */
/*                                                                              */
/*  Please contact Cogenda Pte Ltd for license information                      */
/*                                                                              */
/*  Author: Gong Ding   gdiso@ustc.edu                                          */
/*                                                                              */
/********************************************************************************/



#include "mixA3/mixA3.h"
#include "spice_ckt.h"
#include "parallel.h"
#include "petsc_utils.h"


using PhysicalUnit::kb;
using PhysicalUnit::e;
using PhysicalUnit::cm;
using PhysicalUnit::K;
using PhysicalUnit::A;

/*------------------------------------------------------------------
 * create nonlinear solver contex and adjust some parameters
 */
int MixA3Solver::create_solver()
{

  MESSAGE<< '\n' << "Advanced Mixed Simulation with EBM Level 3 init..." << std::endl;
  RECORD();

  return MixASolverBase::create_solver();
}




/*------------------------------------------------------------------
 * set initial value to solution vector and scaling vector
 */
int MixA3Solver::pre_solve_process(bool load_solution)
{
  if(load_solution)
  {
    // for all the regions
    for(unsigned int n=0; n<_system.n_regions(); n++)
    {
      SimulationRegion * region = _system.region(n);
      region->EBM3_Fill_Value(x, L);
    }

    // for all the bcs
    for(unsigned int b=0; b<_system.get_bcs()->n_bcs(); b++)
    {
      BoundaryCondition * bc = _system.get_bcs()->get_bc(b);
      if(bc->is_spice_electrode())
        bc->MixA_EBM3_Fill_Value(x, L);
      else
        bc->EBM3_Fill_Value(x, L);
    }

    spice_fill_value(x, L);

    VecAssemblyBegin(x);
    VecAssemblyBegin(L);

    VecAssemblyEnd(x);
    VecAssemblyEnd(L);
  }


  return MixASolverBase::pre_solve_process(load_solution);
}





/*------------------------------------------------------------------
 * the main solve routine. which is under the control of ngspice
 */
int MixA3Solver::solve()
{

  START_LOG("solve()", "MixA3Solver");

  switch( SolverSpecify::Type )
  {
      case SolverSpecify::OP :
      solve_dcop(); break;

      case SolverSpecify::DCSWEEP :
      solve_dcsweep(); break;

      case SolverSpecify::TRANSIENT :
      solve_transient(); break;

      default: genius_error();
  }

  STOP_LOG("solve()", "MixA3Solver");

  return 0;
}




/*------------------------------------------------------------------
 * restore the solution to each region
 */
int MixA3Solver::post_solve_process()
{

  VecScatterBegin(scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd  (scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);

  PetscScalar *lxx;
  VecGetArray(lx, &lxx);

  //search for all the regions
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    SimulationRegion * region = _system.region(n);
    region->EBM3_Update_Solution(lxx);
  }

  VecRestoreArray(lx, &lxx);

  _circuit->save_solution();

  return MixASolverBase::post_solve_process();
}


/*------------------------------------------------------------------
 * write the (intermediate) solution to each region
 */
void MixA3Solver::flush_system(Vec v)
{
  VecScatterBegin(scatter, v, lx, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd  (scatter, v, lx, INSERT_VALUES, SCATTER_FORWARD);

  PetscScalar *lxx;
  VecGetArray(lx, &lxx);

  //search for all the regions
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    SimulationRegion * region = _system.region(n);
    region->EBM3_Update_Solution(lxx);
  }

  VecRestoreArray(lx, &lxx);
}



/*------------------------------------------------------------------
 * load previous state into solution vector
 */
int MixA3Solver::diverged_recovery()
{
  // for all the regions
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    SimulationRegion * region = _system.region(n);
    region->EBM3_Fill_Value(x, L);
  }

  //load spice previous solution
  if(Genius::is_last_processor())
    _circuit->restore_solution();

  spice_fill_value(x, L);

  VecAssemblyBegin(x);
  VecAssemblyBegin(L);

  VecAssemblyEnd(x);
  VecAssemblyEnd(L);

  return 0;
}


/*------------------------------------------------------------------
 * Potential Newton Damping
 */
void MixA3Solver::potential_damping(Vec x, Vec y, Vec w, PetscBool *changed_y, PetscBool *changed_w)
{

  PetscScalar    *xx;
  PetscScalar    *yy;
  PetscScalar    *ww;

  VecGetArray(x, &xx);  // previous iterate value
  VecGetArray(y, &yy);  // new search direction and length
  VecGetArray(w, &ww);  // current candidate iterate

  PetscScalar dV_max = 0.0; // the max changes of psi
  const PetscScalar onePerCMC = 1.0*std::pow(cm,-3);
  const PetscScalar T_external = this->get_system().T_external();

  // we should find dV_max;
  // first, we find in local
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    // only consider semiconductor region
    const SimulationRegion * region = _system.region(n);
    if( region->type() != SemiconductorRegion ) continue;

    unsigned int node_psi_offset = region->ebm_variable_offset(POTENTIAL);
    unsigned int node_n_offset   = region->ebm_variable_offset(ELECTRON);
    unsigned int node_p_offset   = region->ebm_variable_offset(HOLE);
    unsigned int node_Tl_offset  = region->ebm_variable_offset(TEMPERATURE);
    unsigned int node_Tn_offset  = region->ebm_variable_offset(E_TEMP);
    unsigned int node_Tp_offset  = region->ebm_variable_offset(H_TEMP);

    SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
    SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
    for(; it!=it_end; ++it)
    {
      const FVM_Node * fvm_node = *it;

      // we konw the position of fvm_node->local_offset() is psi in semiconductor region
      unsigned int local_offset = fvm_node->local_offset();

      if(  std::abs( yy[local_offset + node_psi_offset] ) > dV_max)
        dV_max = std::abs(yy[local_offset + node_psi_offset]);

      //prevent negative carrier density
      if ( ww[local_offset+node_n_offset] < onePerCMC )
        ww[local_offset+node_n_offset] = onePerCMC;
      if ( ww[local_offset+node_p_offset] < onePerCMC )
        ww[local_offset+node_p_offset] = onePerCMC;

      // limit lattice temperature to env temperature - 50k
      if(region->get_advanced_model()->enable_Tl())
      {
        if ( ww[local_offset+node_Tl_offset] < T_external - 50*K )
          ww[local_offset+node_Tl_offset] = T_external - 50*K;
      }
      // electrode temperature should not below 90% of lattice temperature
      if(region->get_advanced_model()->enable_Tn())
      {
        PetscScalar n0=xx[local_offset+node_n_offset];
        PetscScalar n1=ww[local_offset+node_n_offset];
        PetscScalar T0=xx[local_offset+node_Tn_offset]/n0;
        PetscScalar T1=T0*(1-std::min(n1/n0,2.0)) + ww[local_offset+node_Tn_offset]/n0;
        if (T1 < 0.9*T_external)
          T1 = 0.9*T_external;
        ww[local_offset+node_Tn_offset] = T1*n1;
      }
      // hole temperature should not below 90% of lattice temperature
      if(region->get_advanced_model()->enable_Tp())
      {
        PetscScalar p0=xx[local_offset+node_p_offset];
        PetscScalar p1=ww[local_offset+node_p_offset];
        PetscScalar T0=xx[local_offset+node_Tp_offset]/p0;
        PetscScalar T1=T0*(1-std::min(p1/p0,2.0)) + ww[local_offset+node_Tp_offset]/p0;
        if (T1 < 0.9*T_external)
          T1 = 0.9*T_external;
        ww[local_offset+node_Tp_offset] = T1*p1;
      }
    }
  }

  // for parallel situation, we should find the dv_max in global.
  Parallel::max( dV_max );

  if( dV_max > 1e-6 )
  {
    // compute logarithmic potential damping factor f;
    PetscScalar Vt = kb*T_external/e;
    PetscScalar f = log(1+dV_max/Vt)/(dV_max/Vt);

    // do newton damping here
    for(unsigned int n=0; n<_system.n_regions(); n++)
    {
      // only consider semiconductor region
      const SimulationRegion * region = _system.region(n);
      //if( region->type() != SemiconductorRegion ) continue;

      unsigned int node_psi_offset = region->ebm_variable_offset(POTENTIAL);

      SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
      SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
      for(; it!=it_end; ++it)
      {
        const FVM_Node * fvm_node = *it;

        unsigned int local_offset = fvm_node->local_offset();

        ww[local_offset + node_psi_offset] = xx[local_offset + node_psi_offset] - f*yy[local_offset + node_psi_offset];
      }
    }
  }

  //damping spice nodal voltage update
  if(Genius::is_last_processor())
  {
    for(unsigned int n=0; n<_circuit->n_ckt_nodes(); ++n)
    {
      if(_circuit->is_voltage_node(n))
      {
        unsigned int array_offset_x = _circuit->array_offset_x(n);
        PetscScalar dV_max = std::abs(yy[array_offset_x]);
        if (dV_max>5)
        {
          PetscScalar damp_factor = 5/dV_max;
          ww[array_offset_x] = xx[array_offset_x] - damp_factor*yy[array_offset_x];
        }
      }

      if(_circuit->is_current_node(n))
      {
        unsigned int array_offset_x = _circuit->array_offset_x(n);
        PetscScalar dI_max = std::abs(yy[array_offset_x]);
        if (dI_max>1)
        {
          PetscScalar damp_factor = 1/dI_max;
          ww[array_offset_x] = xx[array_offset_x] - damp_factor*yy[array_offset_x];
        }
      }
    }
  }

  VecRestoreArray(x, &xx);
  VecRestoreArray(y, &yy);
  VecRestoreArray(w, &ww);

  *changed_y = PETSC_FALSE;
  *changed_w = PETSC_TRUE;

  return;
}



/*------------------------------------------------------------------
 * Bank-Rose Newton Damping
 */
void MixA3Solver::bank_rose_damping(Vec , Vec , Vec , PetscBool *changed_y, PetscBool *changed_w)
{
  *changed_y = PETSC_FALSE;
  *changed_w = PETSC_FALSE;

  return;
}



/*------------------------------------------------------------------
 * positive density Newton Damping
 */
void MixA3Solver::positive_density_damping(Vec x, Vec y, Vec w, PetscBool *changed_y, PetscBool *changed_w)
{

  PetscScalar    *xx;
  PetscScalar    *yy;
  PetscScalar    *ww;

  VecGetArray(x, &xx);  // previous iterate value
  VecGetArray(y, &yy);  // new search direction and length
  VecGetArray(w, &ww);  // current candidate iterate

  int changed_flag=0;
  const PetscScalar onePerCMC = 1.0*std::pow(cm,-3);
  const PetscScalar T_external = this->get_system().T_external();

  // do newton damping here
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    // only consider semiconductor region
    const SimulationRegion * region = _system.region(n);
    if( region->type() != SemiconductorRegion ) continue;

    unsigned int node_psi_offset = region->ebm_variable_offset(POTENTIAL);
    unsigned int node_n_offset   = region->ebm_variable_offset(ELECTRON);
    unsigned int node_p_offset   = region->ebm_variable_offset(HOLE);
    unsigned int node_Tl_offset  = region->ebm_variable_offset(TEMPERATURE);
    unsigned int node_Tn_offset  = region->ebm_variable_offset(E_TEMP);
    unsigned int node_Tp_offset  = region->ebm_variable_offset(H_TEMP);

    SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
    SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
    for(; it!=it_end; ++it)
    {
      const FVM_Node * fvm_node = *it;

      unsigned int local_offset = fvm_node->local_offset();

      // psi update should not large than 1V
      if ( fabs(yy[local_offset + node_psi_offset]) > 1.0 )
      {
        ww[local_offset + node_psi_offset] = xx[local_offset + node_psi_offset] - std::sign(yy[local_offset + node_psi_offset ])*1.0;
      }

      //prevent negative carrier density
      if ( ww[local_offset+1] < onePerCMC )
      {
        ww[local_offset + node_n_offset] = onePerCMC;
      }

      if ( ww[local_offset + node_p_offset] < onePerCMC )
      {
        ww[local_offset + node_p_offset] = onePerCMC;
      }

      // limit lattice temperature to env temperature - 50k
      if(region->get_advanced_model()->enable_Tl())
      {
        if ( ww[local_offset+node_Tl_offset] < T_external - 50*K )
        { ww[local_offset+node_Tl_offset] = T_external - 50*K;  changed_flag=1;}
      }
      // electrode temperature should not below 90% of lattice temperature
      if(region->get_advanced_model()->enable_Tn())
      {
        PetscScalar n0=xx[local_offset+node_n_offset];
        PetscScalar n1=ww[local_offset+node_n_offset];
        PetscScalar T0=xx[local_offset+node_Tn_offset]/n0;
        PetscScalar T1=T0*(1-std::min(n1/n0,2.0)) + ww[local_offset+node_Tn_offset]/n0;
        if (T1 < 0.9*T_external)
          T1 = 0.9*T_external;
        ww[local_offset+node_Tn_offset] = T1*n1;
      }
      // hole temperature should not below 90% of lattice temperature
      if(region->get_advanced_model()->enable_Tp())
      {
        PetscScalar p0=xx[local_offset+node_p_offset];
        PetscScalar p1=ww[local_offset+node_p_offset];
        PetscScalar T0=xx[local_offset+node_Tp_offset]/p0;
        PetscScalar T1=T0*(1-std::min(p1/p0,2.0)) + ww[local_offset+node_Tp_offset]/p0;
        if (T1 < 0.9*T_external)
          T1 = 0.9*T_external;
        ww[local_offset+node_Tp_offset] = T1*p1;
      }

    }
  }

  VecRestoreArray(x, &xx);
  VecRestoreArray(y, &yy);
  VecRestoreArray(w, &ww);

  *changed_y = PETSC_FALSE;
  *changed_w = PETSC_TRUE;

  return;
}



void MixA3Solver::projection_positive_density_check(Vec x, Vec xo)
{
  PetscScalar    *xx;
  PetscScalar    *oo;

  VecGetArray(x, &xx);
  VecGetArray(xo, &oo);

  const PetscScalar onePerCMC = 1.0*std::pow(cm,-3);
  const PetscScalar T_external = this->get_system().T_external();

  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    // only consider semiconductor region
    const SimulationRegion * region = _system.region(n);
    if( region->type() != SemiconductorRegion ) continue;

    unsigned int node_n_offset   = region->ebm_variable_offset(ELECTRON);
    unsigned int node_p_offset   = region->ebm_variable_offset(HOLE);
    unsigned int node_Tl_offset  = region->ebm_variable_offset(TEMPERATURE);
    unsigned int node_Tn_offset  = region->ebm_variable_offset(E_TEMP);
    unsigned int node_Tp_offset  = region->ebm_variable_offset(H_TEMP);

    SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
    SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
    for(; it!=it_end; ++it)
    {
      const FVM_Node * fvm_node = *it;

      unsigned int local_offset = fvm_node->local_offset();

      //prevent negative carrier density
      if ( xx[local_offset+node_n_offset] < onePerCMC )
        xx[local_offset+node_n_offset]= onePerCMC;

      if ( xx[local_offset+node_p_offset] < onePerCMC )
        xx[local_offset+node_p_offset]= onePerCMC;

      // limit lattice temperature to env temperature - 50k
      if(region->get_advanced_model()->enable_Tl())
      {
        if ( xx[local_offset+node_Tl_offset] < T_external - 50*K )
          xx[local_offset+node_Tl_offset] = T_external - 50*K;
      }

      // electrode temperature should not below 90% of lattice temperature
      if(region->get_advanced_model()->enable_Tn())
      {
        PetscScalar n0=oo[local_offset+node_n_offset];
        PetscScalar n1=xx[local_offset+node_n_offset];
        PetscScalar T0=oo[local_offset+node_Tn_offset]/n0;
        PetscScalar T1=T0*(1-std::min(n1/n0,2.0)) + xx[local_offset+node_Tn_offset]/n0;
        if (T1 < 0.9*T_external)
          T1 = 0.9*T_external;
        xx[local_offset+node_Tn_offset] = T1*n1;
      }

      // hole temperature should not below 90% of lattice temperature
      if(region->get_advanced_model()->enable_Tp())
      {
        PetscScalar p0=oo[local_offset+node_p_offset];
        PetscScalar p1=xx[local_offset+node_p_offset];
        PetscScalar T0=oo[local_offset+node_Tp_offset]/p0;
        PetscScalar T1=T0*(1-std::min(p1/p0,2.0)) + xx[local_offset+node_Tp_offset]/p0;
        if (T1 < 0.9*T_external)
          T1 = 0.9*T_external;
        xx[local_offset+node_Tp_offset] = T1*p1;
      }
    }
  }

  VecRestoreArray(x,&xx);
  VecRestoreArray(xo,&oo);
}


/*------------------------------------------------------------------
 * test if BDF2 can be used for next time step
 */
bool MixA3Solver::BDF2_positive_defined() const
{
  const double r = SolverSpecify::dt_last/(SolverSpecify::dt_last + SolverSpecify::dt);
  const double a = 1.0/(r*(1-r));
  const double b = (1-r)/r;

  unsigned int failure_count=0;

  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    const SimulationRegion * region = _system.region(n);
    if ( region->type() == SemiconductorRegion)
    {
      SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
      SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
      for(; it!=it_end; ++it)
      {
        const FVM_Node * fvm_node = *it;
        const FVM_NodeData * node_data = fvm_node->node_data();

        if(a*node_data->n() < b*node_data->n_last() ) failure_count++;
        if(a*node_data->p() < b*node_data->p_last() ) failure_count++;

        if(region->get_advanced_model()->enable_Tl())
          if(a*node_data->T() < b*node_data->T_last() ) failure_count++;

        if(region->get_advanced_model()->enable_Tn())
          if(a*node_data->n()*node_data->Tn() < b*node_data->n_last()*node_data->Tn_last() ) failure_count++;

        if(region->get_advanced_model()->enable_Tp())
          if(a*node_data->p()*node_data->Tp() < b*node_data->p_last()*node_data->Tp_last() ) failure_count++;
      }
    }
  }

  Parallel::sum(failure_count);
  return failure_count != 0;
}


/*------------------------------------------------------------------
 * evaluate local truncation error
 */
PetscReal MixA3Solver::LTE_norm()
{


  // time steps
  PetscReal hn  = SolverSpecify::dt;
  PetscReal hn1 = SolverSpecify::dt_last;
  PetscReal hn2 = SolverSpecify::dt_last_last;

  // relative error
  PetscReal eps_r = SolverSpecify::TS_rtol;
  // abs error
  PetscReal eps_a = SolverSpecify::TS_atol;

  VecZeroEntries(xp);
  VecZeroEntries(LTE);

  // get the predict solution vector and LTE vector
  if(SolverSpecify::TS_type == SolverSpecify::BDF1)
  {
    VecAXPY(xp, 1+hn/hn1, x_n);
    VecAXPY(xp, -hn/hn1, x_n1);
    VecAXPY(LTE, hn/(hn+hn1), x);
    VecAXPY(LTE, -hn/(hn+hn1), xp);
  }
  else if(SolverSpecify::TS_type == SolverSpecify::BDF2)
  {
    if(SolverSpecify::BDF2_LowerOrder)
    {
      VecAXPY(xp, 1+hn/hn1, x_n);
      VecAXPY(xp, -hn/hn1, x_n1);
      VecAXPY(LTE, hn/(hn+hn1), x);
      VecAXPY(LTE, -hn/(hn+hn1), xp);
    }
    else
    {
      PetscScalar cn  = 1+hn*(hn+2*hn1+hn2)/(hn1*(hn1+hn2));
      PetscScalar cn1 = -hn*(hn+hn1+hn2)/(hn1*hn2);
      PetscScalar cn2 = hn*(hn+hn1)/(hn2*(hn1+hn2));

      VecAXPY(xp, cn,  x_n);
      VecAXPY(xp, cn1, x_n1);
      VecAXPY(xp, cn2, x_n2);
      VecAXPY(LTE, hn/(hn+hn1+hn2),  x);
      VecAXPY(LTE, -hn/(hn+hn1+hn2), xp);
    }
  }

  int N=0;
  PetscReal r;


  // with LTE vector and relative & abs error, we get the error estimate here
  PetscScalar    *xx, *ll;
  VecGetArray(x, &xx);
  VecGetArray(LTE, &ll);

  // error estimate for each region
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    const SimulationRegion * region = _system.region(n);
    switch ( region->type() )
    {
        case SemiconductorRegion :
        {
          unsigned int node_psi_offset = region->ebm_variable_offset(POTENTIAL);
          unsigned int node_n_offset   = region->ebm_variable_offset(ELECTRON);
          unsigned int node_p_offset   = region->ebm_variable_offset(HOLE);
          unsigned int node_Tl_offset  = region->ebm_variable_offset(TEMPERATURE);
          unsigned int node_Tn_offset  = region->ebm_variable_offset(E_TEMP);
          unsigned int node_Tp_offset  = region->ebm_variable_offset(H_TEMP);

          SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
          SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
          for(; it!=it_end; ++it)
          {
            const FVM_Node * fvm_node = *it;

            unsigned int local_offset = fvm_node->local_offset();

            ll[local_offset+node_psi_offset] = 0;
            ll[local_offset+node_n_offset] = ll[local_offset+node_n_offset]/(eps_r*xx[local_offset+node_n_offset]+eps_a);
            ll[local_offset+node_p_offset] = ll[local_offset+node_p_offset]/(eps_r*xx[local_offset+node_p_offset]+eps_a);

            if(region->get_advanced_model()->enable_Tl())
              ll[local_offset+node_Tl_offset] = ll[local_offset+node_Tl_offset]/(eps_r*xx[local_offset+node_Tl_offset]+eps_a); // temperature

            if(region->get_advanced_model()->enable_Tn())
              ll[local_offset+node_Tn_offset] = ll[local_offset+node_Tn_offset]/(eps_r*xx[local_offset+node_Tn_offset]+eps_a); // temperature

            if(region->get_advanced_model()->enable_Tp())
              ll[local_offset+node_Tp_offset] = ll[local_offset+node_Tp_offset]/(eps_r*xx[local_offset+node_Tp_offset]+eps_a); // temperature
          }

          N += (region->ebm_n_variables()-1)*region->n_on_processor_node();

          break;
        }
        case InsulatorRegion :
        case ElectrodeRegion :
        case MetalRegion     :
        {
          unsigned int node_psi_offset = region->ebm_variable_offset(POTENTIAL);
          unsigned int node_Tl_offset  = region->ebm_variable_offset(TEMPERATURE);

          SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
          SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
          for(; it!=it_end; ++it)
          {
            const FVM_Node * fvm_node = *it;

            unsigned int local_offset = fvm_node->local_offset();

            ll[local_offset+node_psi_offset] = 0;

            if(region->get_advanced_model()->enable_Tl())
              ll[local_offset+node_Tl_offset] = ll[local_offset+node_Tl_offset]/(eps_r*xx[local_offset+node_Tl_offset]+eps_a); // temperature
          }

          N += (region->ebm_n_variables()-1)*region->n_on_processor_node();

          break;
        }
        case VacuumRegion:
        break;
        default: genius_error();
    }
  }


  // error estimate for spice ckt
  if(Genius::is_last_processor())
  {
    for(unsigned int n=0; n<_circuit->n_ckt_nodes(); ++n)
    {
      unsigned int array_offset_f = _circuit->array_offset_f(n);
      unsigned int array_offset_x = _circuit->array_offset_x(n);
      ll[array_offset_f] = ll[array_offset_f]/(eps_r*xx[array_offset_x]+eps_a);
    }
    N += _circuit->n_ckt_nodes();
  }

  VecRestoreArray(x, &xx);
  VecRestoreArray(LTE, &ll);

  VecNorm(LTE, NORM_2, &r);

  //for parallel situation, we should sum N of all the processor
  Parallel::sum( N );

  if( N>0 )
  {
    r /= sqrt(static_cast<double>(N));
    return r;
  }

  return 1.0;

}



void MixA3Solver::error_norm()
{
  PetscScalar    *xx;
  PetscScalar    *ff;

  // scatte global solution vector x to local vector lx
  // this is not necessary since it had already done in function evaluation
  //VecScatterBegin(scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);
  //VecScatterEnd  (scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);

  // scatte global function vector f to local vector lf
  VecScatterBegin(scatter, f, lf, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd  (scatter, f, lf, INSERT_VALUES, SCATTER_FORWARD);


  VecGetArray(lx, &xx);  // solution value
  VecGetArray(lf, &ff);  // function value

  // do clear
  potential_norm        = 0;
  electron_norm         = 0;
  hole_norm             = 0;
  temperature_norm      = 0;
  elec_temperature_norm = 0;
  hole_temperature_norm = 0;

  poisson_norm              = 0;
  elec_continuity_norm       = 0;
  hole_continuity_norm       = 0;
  heat_equation_norm        = 0;
  elec_energy_equation_norm = 0;
  hole_energy_equation_norm = 0;
  electrode_norm            = 0;

  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    // only consider semiconductor region
    const SimulationRegion * region = _system.region(n);

    switch ( region->type() )
    {
        case SemiconductorRegion :
        {
          unsigned int node_psi_offset = region->ebm_variable_offset(POTENTIAL);
          unsigned int node_n_offset   = region->ebm_variable_offset(ELECTRON);
          unsigned int node_p_offset   = region->ebm_variable_offset(HOLE);
          unsigned int node_Tl_offset  = region->ebm_variable_offset(TEMPERATURE);
          unsigned int node_Tn_offset  = region->ebm_variable_offset(E_TEMP);
          unsigned int node_Tp_offset  = region->ebm_variable_offset(H_TEMP);

          SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
          SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
          for(; it!=it_end; ++it)
          {
            const FVM_Node * fvm_node = *it;

            unsigned int offset = fvm_node->local_offset();

            potential_norm   += xx[offset+node_psi_offset]*xx[offset+node_psi_offset];
            electron_norm    += xx[offset+node_n_offset]*xx[offset+node_n_offset];
            hole_norm        += xx[offset+node_p_offset]*xx[offset+node_p_offset];

            poisson_norm        += ff[offset+node_psi_offset]*ff[offset+node_psi_offset];
            elec_continuity_norm += ff[offset+node_n_offset]*ff[offset+node_n_offset];
            hole_continuity_norm += ff[offset+node_p_offset]*ff[offset+node_p_offset];

            if(region->get_advanced_model()->enable_Tl())
            {
              temperature_norm    += xx[offset+node_Tl_offset]*xx[offset+node_Tl_offset];
              heat_equation_norm  += ff[offset+node_Tl_offset]*ff[offset+node_Tl_offset];
            }

            if(region->get_advanced_model()->enable_Tn())
            {
              elec_temperature_norm      += xx[offset+node_Tn_offset]/xx[offset+node_n_offset]*xx[offset+node_Tn_offset]/xx[offset+node_n_offset];
              elec_energy_equation_norm  += ff[offset+node_Tn_offset]*ff[offset+node_Tn_offset];
            }

            if(region->get_advanced_model()->enable_Tp())
            {
              hole_temperature_norm      += xx[offset+node_Tp_offset]/xx[offset+node_p_offset]*xx[offset+node_Tp_offset]/xx[offset+node_p_offset];
              hole_energy_equation_norm  += ff[offset+node_Tp_offset]*ff[offset+node_Tp_offset];
            }
          }
          break;
        }
        case InsulatorRegion :
        case ElectrodeRegion :
        case MetalRegion     :
        {
          unsigned int node_psi_offset = region->ebm_variable_offset(POTENTIAL);
          unsigned int node_Tl_offset  = region->ebm_variable_offset(TEMPERATURE);

          SimulationRegion::const_processor_node_iterator it = region->on_processor_nodes_begin();
          SimulationRegion::const_processor_node_iterator it_end = region->on_processor_nodes_end();
          for(; it!=it_end; ++it)
          {
            const FVM_Node * fvm_node = *it;

            unsigned int offset = fvm_node->local_offset();
            potential_norm   += xx[offset+node_psi_offset]*xx[offset+node_psi_offset];
            poisson_norm     += ff[offset+node_psi_offset]*ff[offset+node_psi_offset];

            if(region->get_advanced_model()->enable_Tl())
            {
              temperature_norm    += xx[offset+node_Tl_offset]*xx[offset+node_Tl_offset];
              heat_equation_norm  += ff[offset+node_Tl_offset]*ff[offset+node_Tl_offset];
            }
          }
          break;
        }
        case VacuumRegion:
        break;
        default: genius_error();
    }
  }


  if(Genius::is_last_processor())
    spice_norm = _circuit->ckt_residual_norm2()*A;
  Parallel::broadcast(spice_norm, Genius::last_processor_id());

  // sum of variable value on all processors
  std::vector<PetscScalar> norm_buffer;

  norm_buffer.push_back(potential_norm);
  norm_buffer.push_back(electron_norm);
  norm_buffer.push_back(hole_norm);
  norm_buffer.push_back(temperature_norm);
  norm_buffer.push_back(elec_temperature_norm);
  norm_buffer.push_back(hole_temperature_norm);

  norm_buffer.push_back(poisson_norm);
  norm_buffer.push_back(elec_continuity_norm);
  norm_buffer.push_back(hole_continuity_norm);
  norm_buffer.push_back(heat_equation_norm);
  norm_buffer.push_back(elec_energy_equation_norm);
  norm_buffer.push_back(hole_energy_equation_norm);

  Parallel::sum(norm_buffer);


  // sqrt to get L2 norm
  potential_norm        = sqrt(norm_buffer[0]);
  electron_norm         = sqrt(norm_buffer[1]);
  hole_norm             = sqrt(norm_buffer[2]);
  temperature_norm      = sqrt(norm_buffer[3]);
  elec_temperature_norm = sqrt(norm_buffer[4]);
  hole_temperature_norm = sqrt(norm_buffer[5]);

  poisson_norm              = sqrt(norm_buffer[6]);
  elec_continuity_norm      = sqrt(norm_buffer[7]);
  hole_continuity_norm      = sqrt(norm_buffer[8]);
  heat_equation_norm        = sqrt(norm_buffer[9]);
  elec_energy_equation_norm = sqrt(norm_buffer[10]);
  hole_energy_equation_norm = sqrt(norm_buffer[11]);


  VecRestoreArray(lx, &xx);
  VecRestoreArray(lf, &ff);

}




///////////////////////////////////////////////////////////////
// provide function and jacobian evaluation for DDML1 solver //
///////////////////////////////////////////////////////////////





/*------------------------------------------------------------------
 * evaluate the residual of function f at x
 */
void MixA3Solver::build_petsc_sens_residual(Vec x, Vec r)
{

  START_LOG("MixA3Solver_Residual()", "MixA3Solver");

  // scatte global solution vector x to local vector lx
  VecScatterBegin(scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd  (scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);

  PetscScalar *lxx;
  // get PetscScalar array contains solution from local solution vector lx
  VecGetArray(lx, &lxx);

  // clear old data
  VecZeroEntries (r);

  // flag for indicate ADD_VALUES operator.
  InsertMode add_value_flag = NOT_SET_VALUES;

  // evaluate governing equations of DDML1 in all the regions
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    SimulationRegion * region = _system.region(n);
    region->EBM3_Function(lxx, r, add_value_flag);
  }

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  // evaluate time derivative if necessary
  if(SolverSpecify::TimeDependent == true)
    for(unsigned int n=0; n<_system.n_regions(); n++)
    {
      SimulationRegion * region = _system.region(n);
      region->EBM3_Time_Dependent_Function(lxx, r, add_value_flag);
    }

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  // process hanging node here
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    SimulationRegion * region = _system.region(n);
    region->EBM3_Function_Hanging_Node(lxx, r, add_value_flag);
  }

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  build_spice_function(lxx, r, add_value_flag);

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif


  // preprocess each bc
  VecAssemblyBegin(r);
  VecAssemblyEnd(r);
  std::vector<PetscInt> src_row,  dst_row,  clear_row;
  for(unsigned int b=0; b<_system.get_bcs()->n_bcs(); b++)
  {
    BoundaryCondition * bc = _system.get_bcs()->get_bc(b);
    if(bc->is_spice_electrode())
      bc->MixA_EBM3_Function_Preprocess(lxx, r, src_row, dst_row, clear_row);
    else
      bc->EBM3_Function_Preprocess(lxx, r, src_row, dst_row, clear_row);
  }
  //add source rows to destination rows, and clear rows
  PetscUtils::VecAddClearRow(r, src_row, dst_row, clear_row);
  add_value_flag = NOT_SET_VALUES;

  for(unsigned int b=0; b<_system.get_bcs()->n_bcs(); b++)
  {
    BoundaryCondition * bc = _system.get_bcs()->get_bc(b);
    if(bc->is_spice_electrode())
      bc->MixA_EBM3_Function(lxx, r, add_value_flag);
    else
      bc->EBM3_Function(lxx, r, add_value_flag);
  }

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  // restore array back to Vec
  VecRestoreArray(lx, &lxx);


  // assembly the function Vec
  VecAssemblyBegin(r);
  VecAssemblyEnd(r);

  // scale the function vec
  VecPointwiseMult(r, r, L);

  STOP_LOG("MixA3Solver_Residual()", "MixA3Solver");

}



/*------------------------------------------------------------------
 * evaluate the Jacobian J of function f at x
 */
void MixA3Solver::build_petsc_sens_jacobian(Vec x, Mat *, Mat *)
{

  START_LOG("MixA3Solver_Jacobian()", "MixA3Solver");

  // scatte global solution vector x to local vector lx
  VecScatterBegin(scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd  (scatter, x, lx, INSERT_VALUES, SCATTER_FORWARD);

  PetscScalar *lxx;
  // get PetscScalar array contains solution from local solution vector lx
  VecGetArray(lx, &lxx);

  MatZeroEntries(J);

  // flag for indicate ADD_VALUES operator.
  InsertMode add_value_flag = NOT_SET_VALUES;

  // evaluate Jacobian matrix of governing equations of DDML1 in all the regions
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    SimulationRegion * region = _system.region(n);
    region->EBM3_Jacobian(lxx, &J, add_value_flag);
  }

#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  // evaluate Jacobian matrix of time derivative if necessary
  if(SolverSpecify::TimeDependent == true)
    for(unsigned int n=0; n<_system.n_regions(); n++)
    {
      SimulationRegion * region = _system.region(n);
      region->EBM3_Time_Dependent_Jacobian(lxx, &J, add_value_flag);
    }

  // process hanging node here
  for(unsigned int n=0; n<_system.n_regions(); n++)
  {
    SimulationRegion * region = _system.region(n);
    region->EBM3_Jacobian_Hanging_Node(lxx, &J, add_value_flag);
  }
#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  build_spice_jacobian(lxx, &J, add_value_flag);



  // before first assemble, resereve none zero pattern for each boundary

  if( !jacobian_matrix_first_assemble )
  {
    for(unsigned int b=0; b<_system.get_bcs()->n_bcs(); b++)
    {
      BoundaryCondition * bc = _system.get_bcs()->get_bc(b);
      if(bc->is_spice_electrode())
        bc->MixA_EBM3_Jacobian_Reserve(&J, add_value_flag);
      else
        bc->EBM3_Jacobian_Reserve(&J, add_value_flag);
    }
  }



#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  // evaluate Jacobian matrix of governing equations of EBML3 for all the boundaries
  MatAssemblyBegin(J, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(J, MAT_FINAL_ASSEMBLY);

  // we do not allow zero insert/add to matrix
  if( !jacobian_matrix_first_assemble )
    genius_assert(!MatSetOption(J, MAT_IGNORE_ZERO_ENTRIES, PETSC_TRUE));

  std::vector<PetscInt> src_row,  dst_row,  clear_row;
  for(unsigned int b=0; b<_system.get_bcs()->n_bcs(); b++)
  {
    BoundaryCondition * bc = _system.get_bcs()->get_bc(b);
    if(bc->is_spice_electrode())
      bc->MixA_EBM3_Jacobian_Preprocess(lxx, &J, src_row, dst_row, clear_row);
    else
      bc->EBM3_Jacobian_Preprocess(lxx, &J, src_row, dst_row, clear_row);
  }
  //add source rows to destination rows
  PetscUtils::MatAddRowToRow(J, src_row, dst_row);
  // clear row
  PetscUtils::MatZeroRows(J, clear_row.size(), clear_row.empty() ? NULL : &clear_row[0], 0.0);
  add_value_flag = NOT_SET_VALUES;

  for(unsigned int b=0; b<_system.get_bcs()->n_bcs(); b++)
  {
    BoundaryCondition * bc = _system.get_bcs()->get_bc(b);
    if(bc->is_spice_electrode())
      bc->MixA_EBM3_Jacobian(lxx, &J, add_value_flag);
    else
      bc->EBM3_Jacobian(lxx, &J, add_value_flag);
  }


#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  // restore array back to Vec
  VecRestoreArray(lx, &lxx);

  // assembly the matrix
  MatAssemblyBegin(J, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd  (J, MAT_FINAL_ASSEMBLY);


  //scaling the matrix
  MatDiagonalScale(J, L, PETSC_NULL);

  // we use the reciprocal of matrix diagonal as scaling value
  // this will take place at next call
  //MatGetDiagonal(J, L);
  //VecReciprocal(L);

  //MatView(J, PETSC_VIEWER_DRAW_WORLD);
  //getchar();

  if(!jacobian_matrix_first_assemble)
    jacobian_matrix_first_assemble = true;

  STOP_LOG("MixA3Solver_Jacobian()", "MixA3Solver");


}



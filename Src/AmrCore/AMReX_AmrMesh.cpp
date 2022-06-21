
#include <AMReX.H>
#include <AMReX_AmrMesh.H>
#include <AMReX_Cluster.H>
#include <AMReX_ParmParse.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_Print.H>

#include <AMReX_Bittree.H>

#include <memory>

namespace amrex {

AmrMesh::AmrMesh ()
{
    Geometry::Setup();
    int max_level_in = -1;
    Vector<int> n_cell_in(AMREX_SPACEDIM, -1);
    InitAmrMesh(max_level_in,n_cell_in);
}

AmrMesh::AmrMesh (const RealBox* rb, int max_level_in,
                  const Vector<int>& n_cell_in, int coord,
                  Vector<IntVect> a_refrat, const int* is_per)
{
    Geometry::Setup(rb,coord,is_per);
    InitAmrMesh(max_level_in,n_cell_in, std::move(a_refrat), rb, coord, is_per);
}

AmrMesh::AmrMesh (const RealBox& rb, int max_level_in,
                  const Vector<int>& n_cell_in, int coord,
                  const Vector<IntVect>& a_refrat,
                  const Array<int,AMREX_SPACEDIM>& is_per)
{
    Geometry::Setup(&rb,coord,is_per.data());
    InitAmrMesh(max_level_in,n_cell_in, a_refrat, &rb, coord, is_per.data());
}

AmrMesh::AmrMesh (Geometry const& level_0_geom, AmrInfo const& amr_info)
    : AmrInfo(amr_info)
{
    int nlev = max_level + 1;
    AmrInfo def_amr_info;
    ref_ratio.resize      (nlev, amr_info.ref_ratio.empty()
                           ? def_amr_info.ref_ratio.back()
                           :     amr_info.ref_ratio.back());
    blocking_factor.resize(nlev, amr_info.blocking_factor.empty()
                           ? def_amr_info.blocking_factor.back()
                           :     amr_info.blocking_factor.back());
    max_grid_size.resize  (nlev, amr_info.max_grid_size.empty()
                           ? def_amr_info.max_grid_size.back()
                           :     amr_info.max_grid_size.back());
    n_error_buf.resize    (nlev, amr_info.n_error_buf.empty()
                           ? def_amr_info.n_error_buf.back()
                           :     amr_info.n_error_buf.back());

    dmap.resize(nlev);
    grids.resize(nlev);
    geom.reserve(nlev);
    geom.push_back(level_0_geom);
    for (int lev = 1; lev <= max_level; ++lev) {
        geom.push_back(amrex::refine(geom[lev-1], ref_ratio[lev-1]));
    }

    finest_level = -1;

    if (check_input) { checkInput(); }
}

void
AmrMesh::InitAmrMesh (int max_level_in, const Vector<int>& n_cell_in,
                      Vector<IntVect> a_refrat, const RealBox* rb,
                      int coord, const int* is_per)
{
    ParmParse pp("amr");

    pp.queryAdd("v",verbose);

    if (max_level_in == -1) {
       pp.get("max_level", max_level);
    } else {
       max_level = max_level_in;
    }
    AMREX_ASSERT(max_level >= 0 && max_level < 1000);

    int nlev = max_level + 1;

    blocking_factor.resize(nlev);
    max_grid_size.resize(nlev);
    n_error_buf.resize(nlev);

    geom.resize(nlev);
    dmap.resize(nlev);
    grids.resize(nlev);

    for (int i = 0; i < nlev; ++i) {
        n_error_buf[i]     = IntVect{AMREX_D_DECL(1,1,1)};
        blocking_factor[i] = IntVect{AMREX_D_DECL(8,8,8)};
        max_grid_size[i]   = (AMREX_SPACEDIM == 2) ? IntVect{AMREX_D_DECL(128,128,128)}
                                                   : IntVect{AMREX_D_DECL(32,32,32)};
    }

    // Make the default ref_ratio = 2 for all levels.
    ref_ratio.resize(max_level);
    for (int i = 0; i < max_level; ++i)
    {
      ref_ratio[i] = 2 * IntVect::TheUnitVector();
    }

    pp.queryAdd("n_proper",n_proper);
    pp.queryAdd("grid_eff",grid_eff);
    int cnt = pp.countval("n_error_buf");
    if (cnt > 0) {
        Vector<int> neb;
        pp.getarr("n_error_buf",neb);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            n_error_buf[i] = IntVect(neb[i]);
        }
        for (int i = n; i <= max_level; ++i) {
            n_error_buf[i] = IntVect(neb[cnt-1]);
        }
    }

    cnt = pp.countval("n_error_buf_x");
    if (cnt > 0) {
        int idim = 0;
        Vector<int> neb;
        pp.getarr("n_error_buf_x",neb);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            n_error_buf[i][idim] = neb[i];
        }
        for (int i = n; i <= max_level; ++i) {
            n_error_buf[i][idim] = neb[n-1];
        }
    }

#if (AMREX_SPACEDIM > 1)
    cnt = pp.countval("n_error_buf_y");
    if (cnt > 0) {
        int idim = 1;
        Vector<int> neb;
        pp.getarr("n_error_buf_y",neb);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            n_error_buf[i][idim] = neb[i];
        }
        for (int i = n; i <= max_level; ++i) {
            n_error_buf[i][idim] = neb[n-1];
        }
    }
#endif

#if (AMREX_SPACEDIM == 3)
    cnt = pp.countval("n_error_buf_z");
    if (cnt > 0) {
        int idim = 2;
        Vector<int> neb;
        pp.getarr("n_error_buf_z",neb);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            n_error_buf[i][idim] = neb[i];
        }
        for (int i = n; i <= max_level; ++i) {
            n_error_buf[i][idim] = neb[n-1];
        }
    }
#endif

    // Read in the refinement ratio IntVects as integer AMREX_SPACEDIM-tuples.
    if (max_level > 0)
    {
        const int nratios_vect = max_level*AMREX_SPACEDIM;

        Vector<int> ratios_vect(nratios_vect);

        int got_vect = pp.queryarr("ref_ratio_vect",ratios_vect,0,nratios_vect);

        Vector<int> ratios;

        const int got_int = pp.queryarr("ref_ratio",ratios);

        if (got_int == 1 && got_vect == 1)
        {
            amrex::Abort("Only input *either* ref_ratio or ref_ratio_vect");
        }
        else if (got_vect == 1)
        {
            int k = 0;
            for (int i = 0; i < max_level; i++)
            {
                for (int n = 0; n < AMREX_SPACEDIM; n++,k++) {
                    ref_ratio[i][n] = ratios_vect[k];
                }
            }
        }
        else if (got_int == 1)
        {
            const int ncnt = static_cast<int>(ratios.size());
            for (int i = 0; i < ncnt && i < max_level; ++i)
            {
                for (int n = 0; n < AMREX_SPACEDIM; n++) {
                    ref_ratio[i][n] = ratios[i];
                }
            }
            for (int i = ncnt; i < max_level; ++i)
            {
                for (int n = 0; n < AMREX_SPACEDIM; n++) {
                    ref_ratio[i][n] = ratios.back();
                }
            }
        }
        else
        {
            if (verbose) {
                amrex::Print() << "Using default ref_ratio = 2 at all levels\n";
            }
        }
    }
    //if sent in, this wins over everything.
    if(!a_refrat.empty())
    {
      for (int i = 0; i < max_level; i++)
      {
          ref_ratio[i] = a_refrat[i];
      }
    }

    // Read in max_grid_size.  Use defaults if not explicitly defined.
    cnt = pp.countval("max_grid_size");
    if (cnt > 0) {
        Vector<int> mgs;
        pp.getarr("max_grid_size",mgs);
        int last_mgs = mgs.back();
        mgs.resize(max_level+1,last_mgs);
        SetMaxGridSize(mgs);
    }

    cnt = pp.countval("max_grid_size_x");
    if (cnt > 0) {
        int idim = 0;
        Vector<int> mgs;
        pp.getarr("max_grid_size_x",mgs);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            max_grid_size[i][idim] = mgs[i];
        }
        for (int i = n; i <= max_level; ++i) {
            max_grid_size[i][idim] = mgs[n-1];
        }
    }

#if (AMREX_SPACEDIM > 1)
    cnt = pp.countval("max_grid_size_y");
    if (cnt > 0) {
        int idim = 1;
        Vector<int> mgs;
        pp.getarr("max_grid_size_y",mgs);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            max_grid_size[i][idim] = mgs[i];
        }
        for (int i = n; i <= max_level; ++i) {
            max_grid_size[i][idim] = mgs[n-1];
        }
    }
#endif

#if (AMREX_SPACEDIM == 3)
    cnt = pp.countval("max_grid_size_z");
    if (cnt > 0) {
        int idim = 2;
        Vector<int> mgs;
        pp.getarr("max_grid_size_z",mgs);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            max_grid_size[i][idim] = mgs[i];
        }
        for (int i = n; i <= max_level; ++i) {
            max_grid_size[i][idim] = mgs[n-1];
        }
    }
#endif

    // Read in the blocking_factors.  Use defaults if not explicitly defined.
    cnt = pp.countval("blocking_factor");
    if (cnt > 0) {
        Vector<int> bf;
        pp.getarr("blocking_factor",bf);
        int last_bf = bf.back();
        bf.resize(max_level+1,last_bf);
        SetBlockingFactor(bf);
    }

    cnt = pp.countval("blocking_factor_x");
    if (cnt > 0) {
        int idim = 0;
        Vector<int> bf;
        pp.getarr("blocking_factor_x",bf);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            blocking_factor[i][idim] = bf[i];
        }
        for (int i = n; i <= max_level; ++i) {
            blocking_factor[i][idim] = bf[n-1];
        }
    }

#if (AMREX_SPACEDIM > 1)
    cnt = pp.countval("blocking_factor_y");
    if (cnt > 0) {
        int idim = 1;
        Vector<int> bf;
        pp.getarr("blocking_factor_y",bf);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            blocking_factor[i][idim] = bf[i];
        }
        for (int i = n; i <= max_level; ++i) {
            blocking_factor[i][idim] = bf[n-1];
        }
    }
#endif

#if (AMREX_SPACEDIM == 3)
    cnt = pp.countval("blocking_factor_z");
    if (cnt > 0) {
        int idim = 2;
        Vector<int> bf;
        pp.getarr("blocking_factor_z",bf);
        int n = std::min(cnt, max_level+1);
        for (int i = 0; i < n; ++i) {
            blocking_factor[i][idim] = bf[i];
        }
        for (int i = n; i <= max_level; ++i) {
            blocking_factor[i][idim] = bf[n-1];
        }
    }
#endif

    // Read computational domain and set geometry.
    {
        Vector<int> n_cell(AMREX_SPACEDIM);
        if (n_cell_in[0] == -1)
        {
            pp.getarr("n_cell",n_cell,0,AMREX_SPACEDIM);
        }
        else
        {
            for (int i = 0; i < AMREX_SPACEDIM; i++) { n_cell[i] = n_cell_in[i]; }
        }

        IntVect lo(IntVect::TheZeroVector()), hi(n_cell);
        hi -= IntVect::TheUnitVector();
        Box index_domain(lo,hi);
        for (int i = 0; i <= max_level; i++)
        {
            geom[i].define(index_domain, rb, coord, is_per);
            if (i < max_level) {
                index_domain.refine(ref_ratio[i]);
            }
        }
    }

    //chop up grids to have the number of grids be no less the number of procs
    {
        pp.queryAdd("refine_grid_layout", refine_grid_layout);

        refine_grid_layout_dims = IntVect(refine_grid_layout);
        AMREX_D_TERM(pp.queryAdd("refine_grid_layout_x", refine_grid_layout_dims[0]);,
                     pp.queryAdd("refine_grid_layout_y", refine_grid_layout_dims[1]);,
                     pp.queryAdd("refine_grid_layout_z", refine_grid_layout_dims[2]));

        refine_grid_layout = refine_grid_layout_dims != 0;
    }

    pp.queryAdd("check_input", check_input);

    finest_level = -1;

    if (check_input) { checkInput(); }

    pp.get("use_bittree",use_bittree);
}

int
AmrMesh::MaxRefRatio (int lev) const noexcept
{
    int maxval = 0;
    for (int n = 0; n<AMREX_SPACEDIM; n++) {
        maxval = std::max(maxval,ref_ratio[lev][n]);
    }
    return maxval;
}

void
AmrMesh::SetDistributionMap (int lev, const DistributionMapping& dmap_in) noexcept
{
    ++num_setdm;
    if (dmap[lev] != dmap_in) { dmap[lev] = dmap_in; }
}

void
AmrMesh::SetBoxArray (int lev, const BoxArray& ba_in) noexcept
{
    ++num_setba;
    if (grids[lev] != ba_in) { grids[lev] = ba_in; }
}

void
AmrMesh::SetGeometry (int lev, const Geometry& geom_in) noexcept
{
    geom[lev] = geom_in;
}

int
AmrMesh::GetLevel (Box const& domain) noexcept
{
    Box ccdomain = amrex::enclosedCells(domain);
    for (int lev = 0; lev < geom.size(); ++lev) {
        if (geom[lev].Domain() == ccdomain) { return lev; }
    }
    return -1;
}

void
AmrMesh::ClearDistributionMap (int lev) noexcept
{
    dmap[lev] = DistributionMapping();
}

void
AmrMesh::ClearBoxArray (int lev) noexcept
{
    grids[lev] = BoxArray();
}

bool
AmrMesh::LevelDefined (int lev) noexcept
{
    return lev <= max_level && !grids[lev].empty() && !dmap[lev].empty();
}

void
AmrMesh::ChopGrids (int lev, BoxArray& ba, int target_size) const
{
    if (refine_grid_layout_dims == 0) { return; }

    IntVect chunk = max_grid_size[lev];
    chunk.min(Geom(lev).Domain().length());

    while (ba.size() < target_size)
    {
        IntVect chunk_prev = chunk;

        std::array<std::pair<int,int>,AMREX_SPACEDIM>
            chunk_dir{AMREX_D_DECL(std::make_pair(chunk[0],int(0)),
                                   std::make_pair(chunk[1],int(1)),
                                   std::make_pair(chunk[2],int(2)))};
        std::sort(chunk_dir.begin(), chunk_dir.end());

        for (int idx = AMREX_SPACEDIM-1; idx >= 0; idx--) {
            int idim = chunk_dir[idx].second;
            if (refine_grid_layout_dims[idim]) {
                int new_chunk_size = chunk[idim] / 2;
                if (new_chunk_size != 0 &&
                    new_chunk_size%blocking_factor[lev][idim] == 0)
                {
                    chunk[idim] = new_chunk_size;
                    ba.maxSize(chunk);
                    break;
                }
            }
        }

        if (chunk == chunk_prev) {
            break;
        }
    }
}

BoxArray
AmrMesh::MakeBaseGrids () const
{
    IntVect fac(2);
    const Box& dom = geom[0].Domain();
    const Box dom2 = amrex::refine(amrex::coarsen(dom,2),2);
    for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
        if (dom.length(idim) != dom2.length(idim)) {
            fac[idim] = 1;
        }
    }
    BoxArray ba(amrex::coarsen(dom,fac));
    ba.maxSize(max_grid_size[0]/fac);
    ba.refine(fac);
    // Boxes in ba have even number of cells in each direction
    // unless the domain has odd number of cells in that direction.
    if (refine_grid_layout) {
        ChopGrids(0, ba, ParallelDescriptor::NProcs());
    }
    if (ba == grids[0]) {
        ba = grids[0];  // to avoid duplicates
    }
    PostProcessBaseGrids(ba);
    return ba;
}


void
AmrMesh::MakeNewGrids (int lbase, Real time, int& new_finest, Vector<BoxArray>& new_grids, Vector<DistributionMapping>& new_dmap)
{
    static bool infer_bt_grids = false;

    if(use_bittree) {
        // Initialize BT refinement
        btmesh->refine_init();
        bool grid_changed = false;

        // Do the regular MakeNewGrids and infer refine/derefine from there
        if(infer_bt_grids) {
            MakeNewGrids(lbase,time,new_finest,new_grids);
            for(int lev=lbase+1; lev<=new_finest; ++lev) {
                new_dmap[lev] = DistributionMapping(new_grids[lev]);
            }

            // use grids[lev] and new_grids[lev], infer which boxes are changed
            auto tree0 = btmesh->getTree();
            for(int lev=lbase+1; lev<=new_finest; ++lev) {
                if(grids[lev]==new_grids[lev]) continue;
                grid_changed = true;

                // Use set logic to determine which boxes are new and which
                // no longer exist.
                auto sameBoxes = amrex::intersect(grids[lev],new_grids[lev]);
                auto sameComplement = amrex::complementIn(geom[lev].Domain(),sameBoxes);
                auto newBoxes = amrex::intersect(new_grids[lev],sameComplement);
                auto goneBoxes = amrex::intersect(grids[lev],sameComplement);

                // For boxes that no longer exist, mark parents for derefine
                unsigned parCoord[AMREX_SPACEDIM];
                int plev = lev-1;
                for (Box& bx : goneBoxes.boxList())
                {
                    for(unsigned d=0; d<AMREX_SPACEDIM; ++d)
                        parCoord[d] = bx.smallEnd(d)/(2*max_grid_size[lev][d]);
                    auto p = tree0->identify(plev,parCoord);
                    btmesh->refine_mark(p.id, true);
                }
                
                // For new boxes, mark parents for refine
                for (Box& bx : newBoxes.boxList())
                {
                    for(unsigned d=0; d<AMREX_SPACEDIM; ++d)
                        parCoord[d] = bx.smallEnd(d)/(2*max_grid_size[lev][d]);
                    auto p = tree0->identify(plev,parCoord);
                    btmesh->refine_mark(p.id, true);
                }
            }

            // Not doing btCheckRefine and btCheckDerefine, trusting AMReX algorithm
            MPI_Comm comm = ParallelContext::CommunicatorSub();
            btmesh->refine_reduce(comm);
            btmesh->refine_update();
            if(grid_changed) {
                amrex::Print() << "Mesh changed!" << std::endl;
                amrex::Print() << btmesh->slice_to_string(0) << std::endl;
            }

            // For testing, have BT generate grids and compare to the new_grids made above
            if(grid_changed) {
                Vector<BoxArray> new_grids_bt(new_finest+1);
                Vector<DistributionMapping> new_dmap_bt(new_finest+1);
                int new_finest_bt;
                bool all_good = true;

                btUnit::btCalculateGrids(btmesh,lbase,time,new_finest_bt,new_grids_bt,new_dmap_bt,max_grid_size);

                // Make sure the results from btMakeNewGrids match the results of MakeNewGrids
                if(new_finest != new_finest_bt) all_good = false;
                for(int lev=lbase+1; lev<=new_finest; ++lev) {
                    auto btComplement = amrex::complementIn(geom[lev].Domain(),new_grids_bt[lev]);
                    auto actualMinusBT = amrex::intersect(new_grids[lev],btComplement);
                    if(!actualMinusBT.empty()) all_good = false;
                }
                amrex::Print() << "Comparing BT generated grids to actual grids: ";
                if(all_good) amrex::Print() << "SUCCESS" << std::endl;
                else         amrex::Print() << "ERROR" << std::endl;
            }
        }
        // Use tagging data to mark BT for refinement, then use the new bitmap
        // to calculate the new grids.
        else {
            BL_PROFILE("AmrMesh::MakeNewGrids()");
            BL_ASSERT(lbase < max_level);
            // Add at most one new level
            int max_crse = std::min(finest_level, max_level-1);
            if (new_grids.size() < max_crse+2) new_grids.resize(max_crse+2);
       
            auto tree0 = btmesh->getTree();

            // [1] btTagging - Error Estimation and tagging
            // btTags is indexed by bitid, Bittree's internal indexing scheme.
            // For any id, btTags = 1 if needs refine, -1 if needs derefine.
            std::vector<int> btTags(tree0->id_upper_bound(),0);

            for (int lev=max_crse; lev>=lbase; --lev) {

                TagBoxArray tags(grids[lev],dmap[lev], n_error_buf[lev]);
                ErrorEst(lev, tags, time, 0);

                for (MFIter mfi(tags); mfi.isValid(); ++mfi) {
                    auto const& tagbox = tags.const_array(mfi);
                    bool has_set_tags = amrex::Reduce::AnyOf(mfi.validbox(),
                                                             [=] AMREX_GPU_DEVICE (int i, int j, int k)
                                                             {
                                                                  return tagbox(i,j,k) == TagBox::SET;
                                                             });

                    // Set the value of btTags according to some algorithm. For now,
                    // if any cell is tagged, the whole block is marked for refine.
                    // (btTags = 1 if needs refine, -1 if needs derefine.)
                    if(has_set_tags) {
                        // Calculate the integer coordinates and query BT for the bitid.
                        // For optimization, could cache bitid for each box.
                        unsigned coord[AMREX_SPACEDIM];
		    	        for(int d=0; d<AMREX_SPACEDIM; ++d) {
		    	            coord[d] = grids[lev][mfi].smallEnd(d) / max_grid_size[lev][d];
                        }
		    	        auto b = tree0->identify(lev,coord);
                        if(!b.is_parent and b.level<=max_crse){
                            btTags[b.id] = 1;
                        }
                    }
                }
            }

            // [2] btRefine - check for proper octree nesting, then update bitmap
            MPI_Comm comm = ParallelContext::CommunicatorSub();
            int changed = btUnit::btRefine(btmesh, btTags, comm);
            if(changed>0) {
                amrex::Print() << "Mesh changed!" << std::endl;
                amrex::Print() << btmesh->slice_to_string(0) << std::endl;
            }

            // [3] btCalculateGrids - use new bitmap to generate new grids
            btUnit::btCalculateGrids(btmesh,lbase,time,new_finest,new_grids,new_dmap,max_grid_size);
            // TODO replace default DistributionMapping with a sort over Morton curve
            for(int lev=lbase; lev<=new_finest; ++lev) {
                new_dmap[lev] = DistributionMapping(new_grids[lev]);
            }
        }
       
        // Finalize BT refinement
        btmesh->refine_apply();

    } else {
        MakeNewGrids(lbase,time,new_finest,new_grids);
        for(int lev=lbase+1; lev<=new_finest; ++lev) {
            new_dmap[lev] = DistributionMapping(new_grids[lev]);
        }
    }
}

void
AmrMesh::MakeNewGrids (int lbase, Real time, int& new_finest, Vector<BoxArray>& new_grids)
{
    BL_PROFILE("AmrMesh::MakeNewGrids()");

    BL_ASSERT(lbase < max_level);

    // Add at most one new level
    int max_crse = std::min(finest_level, max_level-1);

    if (new_grids.size() < max_crse+2) { new_grids.resize(max_crse+2); }

    //
    // Construct problem domain at each level.
    //
    Vector<IntVect> bf_lev(max_level); // Blocking factor at each level.
    Vector<IntVect> rr_lev(max_level);
    Vector<Box>     pc_domain(max_level);  // Coarsened problem domain.

    for (int i = 0; i <= max_crse; i++)
    {
        for (int n=0; n<AMREX_SPACEDIM; n++) {
            bf_lev[i][n] = std::max(1,blocking_factor[i+1][n]/ref_ratio[i][n]);
        }
    }
    for (int i = lbase; i < max_crse; i++)
    {
        for (int n=0; n<AMREX_SPACEDIM; n++) {
            // Note that in AmrMesh we check that
            // ref ratio * coarse blocking factor >= fine blocking factor
            rr_lev[i][n] = (ref_ratio[i][n]*bf_lev[i][n])/bf_lev[i+1][n];
        }
    }
    for (int i = lbase; i <= max_crse; i++) {
        pc_domain[i] = amrex::coarsen(Geom(i).Domain(),bf_lev[i]);
    }
    //
    // Construct proper nesting domains.
    //
    Vector<BoxArray> p_n_ba(max_level); // Proper nesting domain.
    Vector<BoxArray> p_n_comp_ba(max_level); // Complement proper nesting domain.
    BoxList p_n, p_n_comp;

    BoxList bl = grids[lbase].simplified_list();
    bl.coarsen(bf_lev[lbase]);
    p_n_comp.parallelComplementIn(pc_domain[lbase],bl);
    bl.clear();
    p_n_comp.simplify();
    p_n_comp.accrete(n_proper);
    if (geom[lbase].isAnyPeriodic()) {
        ProjPeriodic(p_n_comp, pc_domain[lbase], geom[lbase].isPeriodic());
    }

    p_n_comp_ba[lbase].define(std::move(p_n_comp));
    p_n_comp = BoxList();

    p_n.parallelComplementIn(pc_domain[lbase],p_n_comp_ba[lbase]);
    p_n.simplify();

    p_n_ba[lbase].define(std::move(p_n));
    p_n = BoxList();

    for (int i = lbase+1; i <= max_crse; i++)
    {
        p_n_comp = p_n_comp_ba[i-1].boxList();

        // Need to simplify p_n_comp or the number of grids can too large for many levels.
        p_n_comp.simplify();

        p_n_comp.refine(rr_lev[i-1]);
        p_n_comp.accrete(n_proper);

        if (geom[i].isAnyPeriodic()) {
            ProjPeriodic(p_n_comp, pc_domain[i], geom[i].isPeriodic());
        }

        p_n_comp_ba[i].define(std::move(p_n_comp));
        p_n_comp = BoxList();

        p_n.parallelComplementIn(pc_domain[i],p_n_comp_ba[i]);
        p_n.simplify();

        p_n_ba[i].define(std::move(p_n));
        p_n = BoxList();
    }

    //
    // Now generate grids from finest level down.
    //
    new_finest = lbase;

    for (int levc = max_crse; levc >= lbase; levc--)
    {
        int levf = levc+1;
        //
        // Construct TagBoxArray with sufficient grow factor to contain
        // new levels projected down to this level.
        //
        IntVect ngt = n_error_buf[levc];
        BoxArray ba_proj;
        if (levf < new_finest)
        {
            ba_proj = new_grids[levf+1].simplified();
            ba_proj.coarsen(ref_ratio[levf]);
            ba_proj.growcoarsen(n_proper, ref_ratio[levc]);

            BoxArray levcBA = grids[levc].simplified();
            int ngrow = 0;
            while (!levcBA.contains(ba_proj))
            {
                levcBA.grow(1);
                ++ngrow;
            }
            ngt.max(IntVect(ngrow));
        }
        TagBoxArray tags(grids[levc],dmap[levc],ngt);

        //
        // Only use error estimation to tag cells for the creation of new grids
        //      if the grids at that level aren't already fixed.
        //

        if ( ! (useFixedCoarseGrids() && levc < useFixedUpToLevel()) ) {
            ErrorEst(levc, tags, time, 0);
        }
        //

        //
        // Buffer error cells.
        //
        tags.buffer(n_error_buf[levc]);

        if (useFixedCoarseGrids())
        {
            if (levc>=useFixedUpToLevel())
            {
                tags.setVal(GetAreaNotToTag(levc), TagBox::CLEAR);
            }
            else
            {
                new_finest = std::max(new_finest,levf);
            }
        }

        //
        // Coarsen the taglist by blocking_factor/ref_ratio.
        //
        int bl_max = 0;
        for (int n=0; n<AMREX_SPACEDIM; n++) {
            bl_max = std::max(bl_max,bf_lev[levc][n]);
        }
        if (bl_max >= 1) {
            tags.coarsen(bf_lev[levc]);
        } else {
            amrex::Abort("blocking factor is too small relative to ref_ratio");
        }
        //
        // Remove or add tagged points which violate/satisfy additional
        // user-specified criteria.
        //
        ManualTagsPlacement(levc, tags, bf_lev);
        //
        // If new grids have been constructed above this level, project
        // those grids down and tag cells on intersections to ensure
        // proper nesting.
        //
        if (levf < new_finest) {
            ba_proj.coarsen(bf_lev[levc]);
            tags.setVal(ba_proj,TagBox::SET);
        }
        //
        // Map tagged points through periodic boundaries, if any.
        //
        tags.mapPeriodicRemoveDuplicates(Geometry(pc_domain[levc],
                                                  Geom(levc).ProbDomain(),
                                                  Geom(levc).CoordInt(),
                                                  Geom(levc).isPeriodic()));
        //
        // Remove cells outside proper nesting domain for this level.
        //
        tags.setVal(p_n_comp_ba[levc],TagBox::CLEAR);
        p_n_comp_ba[levc].clear();
        //
        // Create initial cluster containing all tagged points.
        //
        Gpu::PinnedVector<IntVect> tagvec;
        tags.collate(tagvec);
        tags.clear();

        if (!tagvec.empty())
        {
            //
            // Created new level, now generate efficient grids.
            //
            if ( !(useFixedCoarseGrids() && levc<useFixedUpToLevel()) ) {
                new_finest = std::max(new_finest,levf);
            }

            if (levf > useFixedUpToLevel()) {
                BoxList new_bx;
                if (ParallelDescriptor::IOProcessor()) {
                    BL_PROFILE("AmrMesh-cluster");
                    //
                    // Construct initial cluster.
                    //
                    ClusterList clist(tagvec.data(), static_cast<Long>(tagvec.size()));
                    if (use_new_chop) {
                        clist.new_chop(grid_eff);
                    } else {
                        clist.chop(grid_eff);
                    }
                    clist.intersect(p_n_ba[levc]);
                    //
                    // Efficient properly nested Clusters have been constructed
                    // now generate list of grids at level levf.
                    //
                    clist.boxList(new_bx);
                    new_bx.refine(bf_lev[levc]);
                    new_bx.simplify();

                    if (new_bx.size()>0) {
                        // Chop new grids outside domain
                        new_bx.intersect(Geom(levc).Domain());
                    }
                }
                new_bx.Bcast();  // Broadcast the new BoxList to other processes

                //
                // Refine up to levf.
                //
                new_bx.refine(ref_ratio[levc]);
                BL_ASSERT(new_bx.isDisjoint());

                new_grids[levf] = BoxArray(std::move(new_bx), max_grid_size[levf]);
            }
        }
    }

#if 0
    if (!useFixedCoarseGrids()) {
        // check proper nesting
        // This check does not consider periodic boundary and could fail if
        // the blocking factor is not the same on all levels.
        for (int lev = lbase+1; lev <= new_finest; ++lev) {
            BoxArray const& cba = (lev == lbase+1) ? grids[lev-1] : new_grids[lev-1];
            BoxArray const& fba = amrex::coarsen(new_grids[lev],ref_ratio[lev-1]);
            IntVect np = bf_lev[lev-1] * n_proper;
            Box const& cdomain = Geom(lev-1).Domain();
            for (int i = 0, N = fba.size(); i < N; ++i) {
                Box const& fb = amrex::grow(fba[i],np) & cdomain;
                if (!cba.contains(fb,true)) {
                    amrex::Abort("AmrMesh::MakeNewGrids: new grids not properly nested");
                }
            }
        }
    }
#endif

    for (int lev = lbase+1; lev <= new_finest; ++lev) {
        if (new_grids[lev].empty())
        {
            if (!(useFixedCoarseGrids() && lev<useFixedUpToLevel()) ) {
                amrex::Abort("AmrMesh::MakeNewGrids: how did this happen?");
            }
        }
        else if (refine_grid_layout)
        {
            ChopGrids(lev,new_grids[lev],ParallelDescriptor::NProcs());
            if (new_grids[lev] == grids[lev]) {
                new_grids[lev] = grids[lev]; // to avoid duplicates
            }
        }
    }
}

void
AmrMesh::MakeNewGrids (Real time)
{
    // define coarse level BoxArray and DistributionMap
    {
        finest_level = 0;

        const BoxArray& ba = MakeBaseGrids();
        DistributionMapping dm(ba);
        const auto old_num_setdm = num_setdm;
        const auto old_num_setba = num_setba;

        //Initialize Bittree
        if(use_bittree) {

            // top = number of grids on coarsest level in each direction
            // TODO better way to do this?
            std::vector<int> top(AMREX_SPACEDIM,0);
            IntVect ncells = geom[0].Domain().length();
            for(int i=0; i<AMREX_SPACEDIM; ++i) {
                top[i] = ncells[i] / max_grid_size[0][i];
            }

            // includes = boolean to check each coarsest level grid exists
            // (Bittree supports having "holes" in the mesh)
            int ngrids = AMREX_D_TERM(top[0],*top[1],*top[2]);
            std::vector<int> includes(ngrids,1);

            amrex::Print() << "Initializing Bittree..." << std::endl;
            btmesh = std::make_shared<bittree::BittreeAmr>(top.data(),includes.data());
        }

        MakeNewLevelFromScratch(0, time, ba, dm);

        if (old_num_setba == num_setba) {
            SetBoxArray(0, ba);
        }
        if (old_num_setdm == num_setdm) {
            SetDistributionMap(0, dm);
        }
    }

    if (max_level > 0) // build fine levels
    {
        Vector<BoxArray> new_grids(max_level+1);
        Vector<DistributionMapping> new_dmap(max_level+1);
        new_grids[0] = grids[0];
        new_dmap[0] = dmap[0];
        do
        {
            int new_finest;

            // Add (at most) one level at a time.
            MakeNewGrids(finest_level,time,new_finest,new_grids,new_dmap);

            if (new_finest <= finest_level) { break; }
            finest_level = new_finest;

            //const auto old_num_setdm = num_setdm;

            MakeNewLevelFromScratch(new_finest, time, new_grids[finest_level], new_dmap[finest_level]);

            SetBoxArray(new_finest, new_grids[new_finest]);
            //if (old_num_setdm == num_setdm) {
                SetDistributionMap(new_finest, new_dmap[new_finest]);
            //}
        }
        while (finest_level < max_level);

        // Iterate grids to ensure fine grids encompass all interesting junk.
        if (iterate_on_new_grids)
        {
            for (int it=0; it<4; ++it)  // try at most 4 times
            {
                for (int i = 1; i <= finest_level; ++i) {
                    new_grids[i] = grids[i];
                    new_dmap[i] = dmap[i];
                }

                int new_finest;
                MakeNewGrids(0, time, new_finest, new_grids,new_dmap);

                if (new_finest < finest_level) { break; }
                finest_level = new_finest;

                bool grids_the_same = true;
                for (int lev = 1; lev <= new_finest; ++lev) {
                    if (new_grids[lev] != grids[lev] || new_dmap[lev] != dmap[lev]) {
                        grids_the_same = false;
                        //const auto old_num_setdm = num_setdm;

                        MakeNewLevelFromScratch(lev, time, new_grids[lev], new_dmap[lev]);

                        SetBoxArray(lev, new_grids[lev]);
                        //if (old_num_setdm == num_setdm) {
                            SetDistributionMap(lev, new_dmap[lev]);
                        //}
                    }
                }
                if (grids_the_same) { break; }
            }
        }
    }
}

void
AmrMesh::ProjPeriodic (BoxList& blout, const Box& domain,
                       Array<int,AMREX_SPACEDIM> const& is_per)
{
    //
    // Add periodic translates to blout.
    //

    BoxList blorig(blout);

    int nist = -1;
    int niend = 1;
#if (AMREX_SPACEDIM < 2)
    int njst = 0;
    int njend = 0;
#else
    int njst = -1;
    int njend = 1;
#endif
#if (AMREX_SPACEDIM < 3)
    int nkst = 0;
    int nkend = 0;
#else
    int nkst = -1;
    int nkend = 1;
#endif

    int ri,rj,rk;
    for (ri = nist; ri <= niend; ri++)
    {
        if (ri != 0 && !is_per[0]) {
            continue;
        }
        if (ri != 0 && is_per[0]) {
            blorig.shift(0,ri*domain.length(0));
        }
        for (rj = njst; rj <= njend; rj++)
        {
            if (rj != 0 && !is_per[1]) {
                continue;
            }
            if (rj != 0 && is_per[1]) {
                blorig.shift(1,rj*domain.length(1));
            }
            for (rk = nkst; rk <= nkend; rk++)
            {
                if (rk != 0 && !is_per[2]) {
                    continue;
                }
                if (rk != 0 && is_per[2]) {
                    blorig.shift(2,rk*domain.length(2));
                }

                BoxList tmp(blorig);
                tmp.intersect(domain);
                blout.catenate(tmp);

                if (rk != 0 && is_per[2]) {
                    blorig.shift(2,-rk*domain.length(2));
                }
            }
            if (rj != 0 && is_per[1]) {
                blorig.shift(1,-rj*domain.length(1));
            }
        }
        if (ri != 0 && is_per[0]) {
            blorig.shift(0,-ri*domain.length(0));
        }
    }
}

void
AmrMesh::checkInput ()
{
    if (max_level < 0) {
        amrex::Error("checkInput: max_level not set");
    }

    //
    // Check level dependent values.
    //
    for (int i = 0; i < max_level; i++)
    {
        if (MaxRefRatio(i) < 2) {
            amrex::Error("Amr::checkInput: bad ref_ratios");
        }
    }

    const Box& domain = Geom(0).Domain();
    if (!domain.ok()) {
        amrex::Error("level 0 domain bad or not set");
    }

    //
    // Check that domain size is a multiple of blocking_factor[0].
    //   (only check if blocking_factor <= max_grid_size)
    //
    for (int idim = 0; idim < AMREX_SPACEDIM; idim++)
    {
        int len = domain.length(idim);
        if (blocking_factor[0][idim] <= max_grid_size[0][idim]) {
           if (len%blocking_factor[0][idim] != 0)
           {
              amrex::Print() << "domain size in direction " << idim << " is " << len << std::endl;
              amrex::Print() << "blocking_factor is " << blocking_factor[0][idim] << std::endl;
              amrex::Error("domain size not divisible by blocking_factor");
           }
        }
    }

    //
    // Check that blocking_factor is a power of 2.
    //
    for (int i = 0; i <= max_level; i++)
    {
        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim)
        {
            int k = blocking_factor[i][idim];
            while ( k > 0 && (k%2 == 0) ) {
                k /= 2;
            }
            if (k != 1) {
                amrex::Error("Amr::checkInput: blocking_factor not power of 2. You can bypass this by setting ParmParse runtime parameter amr.check_input=0, although we do not recommend it.");
            }
        }
    }

    //
    // Check that blocking_factor does not vary too much between levels
    //
    for (int i = 0; i < max_level; i++) {
        const IntVect bfrr = blocking_factor[i] * ref_ratio[i];
        if (!bfrr.allGE(blocking_factor[i+1])) {
            amrex::Print() << "Blocking factors on levels " << i << " and " << i+1
                           << " are " << blocking_factor[i] << " " << blocking_factor[i+1]
                           << ". Ref ratio is " << ref_ratio[i]
                           << ".  They vary too much between levels." << std::endl;
            amrex::Error("Blocking factors vary too much between levels");
        }
    }

    //
    // Check that max_grid_size is a multiple of blocking_factor at every level.
    //   (only check if blocking_factor <= max_grid_size)
    //
    for (int i = 0; i < max_level; i++)
    {
        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
            if (blocking_factor[i][idim] <= max_grid_size[i][idim]) {
                if (max_grid_size[i][idim]%blocking_factor[i][idim] != 0) {
                    amrex::Print() << "max_grid_size in direction " << idim
                                   << " is " << max_grid_size[i][idim] << std::endl;
                    amrex::Print() << "blocking_factor is " << blocking_factor[i][idim] << std::endl;
                    amrex::Error("max_grid_size not divisible by blocking_factor");
                }
            }
        }
    }

    // Make sure TagBoxArray has no overlapped valid cells after coarsening by block_factor/ref_ratio
    for (int i = 0; i < max_level; ++i) {
        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
            int bf_lev = std::max(1,blocking_factor[i+1][idim]/ref_ratio[i][idim]);
            int min_grid_size = std::min(blocking_factor[i][idim],max_grid_size[i][idim]);
            if (min_grid_size % bf_lev != 0) {
                amrex::Print() << "On level " << i << " in direction " << idim
                               << " max_grid_size is " << max_grid_size[i][idim]
                               << " blocking factor is " << blocking_factor[i][idim] << "\n"
                               << "On level " << i+1 << " in direction " << idim
                               << " blocking_factor is " << blocking_factor[i+1][idim] << std::endl;
                amrex::Error("Coarse level blocking factor not a multiple of fine level blocking factor divided by ref ratio");
            }
        }
    }

    if( ! (Geom(0).ProbDomain().volume() > 0.0) ) {
        amrex::Error("Amr::checkInput: bad physical problem size");
    }

    if(verbose > 0) {
        amrex::Print() << "Successfully read inputs file ... " << '\n';
    }
}

long
AmrMesh::CountCells (int lev) noexcept
{
    return grids[lev].numPts();
}

std::ostream& operator<< (std::ostream& os, AmrMesh const& amr_mesh)
{
    os << "  verbose = " << amr_mesh.verbose << "\n";
    os << "  max_level = " << amr_mesh.max_level << "\n";
    os << "  ref_ratio =";
    for (int lev = 0; lev < amr_mesh.max_level; ++lev) { os << " " << amr_mesh.ref_ratio[lev]; }
    os << "\n";
    os << "  blocking_factor =";
    for (int lev = 0; lev <= amr_mesh.max_level; ++lev) { os << " " << amr_mesh.blocking_factor[lev]; }
    os << "\n";
    os << "  max_grid_size =";
    for (int lev = 0; lev <= amr_mesh.max_level; ++lev) { os << " " << amr_mesh.max_grid_size[lev]; }
    os << "\n";
    os << "  n_error_buf =";
    for (int lev = 0; lev < amr_mesh.max_level; ++lev) { os << " " << amr_mesh.n_error_buf[lev]; }
    os << "\n";
    os << "  grid_eff = " << amr_mesh.grid_eff << "\n";
    os << "  n_proper = " << amr_mesh.n_proper << "\n";
    os << "  use_fixed_upto_level = " << amr_mesh.use_fixed_upto_level << "\n";
    os << "  use_fixed_coarse_grids = " << amr_mesh.use_fixed_coarse_grids << "\n";
    os << "  refine_grid_layout_dims = " << amr_mesh.refine_grid_layout_dims << "\n";
    os << "  check_input = " << amr_mesh.check_input  << "\n";
    os << "  use_new_chop = " << amr_mesh.use_new_chop << "\n";
    os << "  iterate_on_new_grids = " << amr_mesh.iterate_on_new_grids << "\n";
    return os;
}

}

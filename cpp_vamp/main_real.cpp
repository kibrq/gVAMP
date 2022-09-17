#include <mpi.h>
#include <iostream>
#include <cmath> // contains definition of ceil
#include <bits/stdc++.h>  // contains definition of INT_MAX
#include <immintrin.h> // contains definition of _mm_malloc
#include <numeric>
#include "utilities.hpp"
#include "data.hpp"
#include "vamp.hpp"
#include "options.hpp"


int main(int argc, char** argv)
{

    // starting parallel processes
    int required_MPI_level = MPI_THREAD_MULTIPLE;
    int provided_MPI_level;
    MPI_Init_thread(NULL, NULL, required_MPI_level, &provided_MPI_level);
    //MPI_Init(NULL, NULL);

    const Options opt(argc, argv);

    int rank = 0;
    int nranks = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);
    std::cout << "rank = " << rank << std::endl;
    

    //%%%%%%%%%%%%%%%%%%%%%%%%%%%
    // setting blocks of markers 
    //%%%%%%%%%%%%%%%%%%%%%%%%%%%

    size_t Mt = opt.get_Mt();
    size_t N = opt.get_N();

    const int modu = Mt % nranks;
    const int size = Mt / nranks;

    int Mm = Mt % nranks != 0 ? size + 1 : size;

    int len[nranks], start[nranks];
    int cum = 0;
    for (int i=0; i<nranks; i++) {
        len[i]  = i < modu ? size + 1 : size;
        start[i] = cum;
        cum += len[i];
    }
    assert(cum == Mt);

    int M = len[rank];
    int S = start[rank];  // task marker start

    printf("INFO   : rank %4d has %d markers over tot Mt = %d, max Mm = %d, starting at S = %d\n", rank, M, Mt, Mm, S);


    //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    // reading genotype data / phenotype file
    //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

    //const std::string bedfp = "/nfs/scistore13/robingrp/human_data/geno/ldp08/ukb22828_UKB_EST_v3_ldp005_maf01.bed";
    //std::string phenfp = "/nfs/scistore13/robingrp/human_data/geno/ldp08/ukb_ht_noNA.phen";
   
    // reading signal estimate from gmrm software
    const std::string true_beta_height = "/nfs/scistore13/robingrp/human_data/adepope_preprocessing/VAMPJune2022/height_true.txt";

    int run_on_test_set = 1;

    if (run_on_test_set == 0){ // we distinguish between calculation of R2 on the test set or not (both version load the output from gmrm)
        
        const std::string true_beta_height = "/nfs/scistore13/robingrp/human_data/adepope_preprocessing/VAMPJune2022/height_true.txt";
        std::vector<double> beta_true = read_vec_from_file(true_beta_height, M, S);
        
        std::string phenfp = (opt.get_phen_files())[0];
        data dataset(phenfp, opt.get_bed_file(), opt.get_N(), M, opt.get_Mt(), S, rank);
        dataset.read_phen();
        dataset.read_genotype_data();
        dataset.compute_markers_statistics();
        

        //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
        // running EM-VAMP algorithm on the data
        //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    

        double gam1 = 1e-6; //, gamw = 2.112692482840060;
        double gamw = 2;
        vamp emvamp(M, gam1, gamw, beta_true, rank, opt);
        std::vector<double> x_est = emvamp.infere(&dataset);
    
    }
    else if (run_on_test_set == 1)
    {

        std::cout << "mode: verifying on test set" << std::endl;

        const std::string true_beta_height = "/nfs/scistore13/robingrp/human_data/adepope_preprocessing/VAMPJune2022/height_true.txt";
        std::vector<double> beta_true = read_vec_from_file(true_beta_height, M, S);
  
        std::string phenfp = (opt.get_phen_files())[0]; // currently only one phenotype file is supported
        data dataset(phenfp, opt.get_bed_file(), opt.get_N(), M, opt.get_Mt(), S, rank);
        dataset.read_phen();
        dataset.read_genotype_data();
        dataset.compute_markers_statistics();

        double gam1 = 1e-6; //, gamw = 2.112692482840060;
        double gamw = 2;
        vamp emvamp(M, gam1, gamw, beta_true, rank, opt);
        std::vector<double> x_est = emvamp.infere(&dataset);

        double intercept = dataset.get_intercept();
        double scale = dataset.get_scale();
        if (rank == 0){
            std::cout << "intercept = " << intercept << std::endl;
            std::cout << "scale = " << scale << std::endl;
        }
                
        int final_it = opt.get_iterations();

        // reading test set
        const std::string bedfp_HTtest = "/nfs/scistore13/robingrp/human_data/adepope_preprocessing/VAMPJune2022/cpp_VAMP/ukb22828_UKB_EST_v3_ldp08_test_HT.bed";
        const std::string pheno_HTtest = "/nfs/scistore13/robingrp/human_data/pheno/continuous/ukb_test_HT.phen";

        int N_test = 15000;
        int Mt_test = Mt;
        int M_test = M;

        data dataset_test(pheno_HTtest, bedfp_HTtest, N_test, M_test, Mt_test, S, rank);
        dataset_test.read_phen();
        dataset_test.read_genotype_data();
        dataset_test.compute_markers_statistics();

        
        std::vector<double> x_est_scaled = x_est;
        for (int i0 = 0; i0 < x_est_scaled.size(); i0++)
            x_est_scaled[i0] = x_est[i0] * sqrt( (double) N_test / (double) N );

        //if (rank == 0)
        //    std::cout << "std x_est_scaled = " << calc_stdev(x_est_scaled) << std::endl;

        std::vector<double> z = dataset_test.Ax(x_est_scaled.data());

        // mean and std of z
        double stdev_z = calc_stdev(z);
        //if (rank == 0)
        //    std::cout << "z stdev^2 = " << stdev_z * stdev_z << std::endl;

        //if (rank == 0)
        //std::cout << "z[0] = " << z[0] << ", rank = "<< rank << std::endl;
        for (int i0 = 0; i0 < z.size(); i0++ ){
            z[i0] = intercept + scale * z[i0];
        }
        //if (rank == 0)
        //    std::cout << "after z[0] = " << z[0] << ", rank = " << rank << std::endl;

        std::vector<double> y_test = dataset_test.get_phen();

        double l2_pred_err2 = 0;
        for (int i0 = 0; i0 < N_test; i0++){
            l2_pred_err2 += (y_test[i0] - z[i0]) * (y_test[i0] - z[i0]);
        }

        double stdev = calc_stdev(y_test);
        if (rank == 0)
            std::cout << "y stdev^2 = " << stdev * stdev << std::endl;
        

        if (rank == 0){
            std::cout << "test l2 pred err = " << l2_pred_err2 << std::endl;
            std::cout << "test R2 = " << 1 - l2_pred_err2 / ( stdev * stdev * y_test.size() ) << std::endl;
        }

    }

    MPI_Finalize();
    return 0;
}
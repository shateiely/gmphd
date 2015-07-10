#include "gmphd_filter.h"


// Author : Benjamin Lefaudeux (blefaudeux@github)


GMPHD::GMPHD(int max_gaussians,
             int dimension,
             bool motion_model,
             bool verbose):m_maxGaussians(max_gaussians),
    m_dimMeasures(dimension),
    m_motionModel(motion_model),
    m_bVerbose(verbose){

    if (!motion_model)
        m_dimState = m_dimMeasures;
    else
        m_dimState = 2 * m_dimMeasures;

    // Initiate matrices :
    I = MatrixXf::Identity(m_dimState, m_dimState);
}

void  GMPHD::buildUpdate ()
{
    MatrixXf temp_matrix(m_dimState, m_dimState);

    // Concatenate all the wannabe targets :
    // - birth targets
    m_iBirthTargets.clear();

    if(m_birthTargets.m_gaussians.size () > 0)
    {
        for (unsigned int i=0; i<m_birthTargets.m_gaussians.size (); ++i)
        {
            m_iBirthTargets.push_back (m_expTargets.m_gaussians.size () + i);
        }

        m_expTargets.m_gaussians.insert (m_expTargets.m_gaussians.end (),
                                              m_birthTargets.m_gaussians.begin (),
                                              m_birthTargets.m_gaussians.begin () + m_birthTargets.m_gaussians.size ());
    }

    // - spawned targets
    if (m_spawnTargets.m_gaussians.size () > 0)
    {
        m_expTargets.m_gaussians.insert (m_expTargets.m_gaussians.end (),
                                              m_spawnTargets.m_gaussians.begin (),
                                              m_spawnTargets.m_gaussians.begin () + m_spawnTargets.m_gaussians.size ());
    }

    if (m_bVerbose)
    {
        printf("GMPHD : inserted %zu birth targets, now %zu expected\n",
               m_birthTargets.m_gaussians.size (), m_expTargets.m_gaussians.size());

        m_birthTargets.print ();

        printf("GMPHD : inserted %zu spawned targets, now %zu expected\n",
               m_spawnTargets.m_gaussians.size (), m_expTargets.m_gaussians.size());

        m_spawnTargets.print ();
    }

    // Compute PHD update components (for every expected target)
    m_nPredictedTargets = m_expTargets.m_gaussians.size ();

    m_expMeasure.resize (m_nPredictedTargets);
    m_expDisp.resize (m_nPredictedTargets);
    m_uncertainty.resize (m_nPredictedTargets);
    m_covariance.resize (m_nPredictedTargets);

    for (int i=0; i< m_nPredictedTargets; ++i)
    {
        // Compute the expected measurement
        m_expMeasure[i] = m_obsMat * m_expTargets.m_gaussians[i].m_mean;

        m_expDisp[i] = m_obsCov + m_obsMat * m_expTargets.m_gaussians[i].m_cov * m_obsMatT;

        if (isnan(m_expDisp[i](0,0)))
        {
            printf("NaN value in dispersion\n");
            cout << "Expected cov \n" << m_expTargets.m_gaussians[i].m_cov << endl << endl;
            THROW_ERR("NaN in GMPHD Update process");
        }

        temp_matrix = m_expDisp[i].inverse();

        m_uncertainty[i] =  m_expTargets.m_gaussians[i].m_cov * m_obsMatT * temp_matrix;

        m_covariance[i] = (I - m_uncertainty[i]*m_obsMat) * m_expTargets.m_gaussians[i].m_cov;
    }
}

void    GMPHD::extractTargets(float threshold)
{
    // Deal with crappy settings
    float thld = max(threshold, 0.f);

    // Get trough every target, keep the ones whose weight is above threshold
    m_extractedTargets.m_gaussians.clear();

    for (unsigned int i=0; i<m_currTargets.m_gaussians.size(); ++i)
    {
        if (m_currTargets.m_gaussians[i].m_weight >= thld)
        {
            m_extractedTargets.m_gaussians.push_back(m_currTargets.m_gaussians[i]);
        }
    }

    printf("GMPHD_extract : %zu targets\n", m_extractedTargets.m_gaussians.size ());
}

void    GMPHD::getTrackedTargets(const float extract_thld, vector<float> &position,
                                 vector<float> &speed, vector<float> &weight)
{
    // Fill in "extracted_targets" from the "current_targets"
    extractTargets(extract_thld);

    position.clear();
    speed.clear();
    weight.clear();

    for (auto const & gaussian : m_extractedTargets.m_gaussians)
    {
        for (int j=0; j<m_dimMeasures; ++j)
        {
            position.push_back(gaussian.m_mean(j,0));
            speed.push_back(gaussian.m_mean(m_dimMeasures + j,0));
        }

        weight.push_back(gaussian.m_weight);
    }
}


float   GMPHD::gaussDensity(MatrixXf const & point, MatrixXf const & mean, MatrixXf const & cov)
{
    MatrixXf cov_inverse, mismatch;

    float det, res, dist;

    det         = cov.block(0, 0, m_dimMeasures, m_dimMeasures).determinant();
    cov_inverse = cov.block(0, 0, m_dimMeasures, m_dimMeasures).inverse();
    mismatch    = point.block(0,0,m_dimMeasures, 1) - mean.block(0,0,m_dimMeasures,1);

    Matrix <float,1,1> distance = mismatch.transpose() * cov_inverse * mismatch;

    distance /= -2.f;

    dist =  (float) distance(0,0);
    res = 1.f/(pow(2*M_PI, m_dimMeasures) * sqrt(fabs(det)) * exp(dist));

    return res;
}

float   GMPHD::gaussDensity_3D(const Matrix <float, 3,1> &point,
                               const Matrix <float, 3,1> &mean,
                               const Matrix <float, 3,3> &cov)
{
    float det, res;

    Matrix <float, 3, 3> cov_inverse;
    Matrix <float, 3, 1> mismatch;

    det = cov.determinant();
    cov_inverse = cov.inverse();

    mismatch = point - mean;

    Matrix <float, 1, 1> distance = mismatch.transpose() * cov_inverse * mismatch;

    distance /= -2.f;

    // Deal with faulty determinant case
    if (det == 0.f)
    {
        return 0.f;
    }

    res = 1.f/sqrt(pow(2*M_PI, 3) * fabs(det)) * exp(distance.coeff (0,0));

    if (isinf(det))
    {
        printf("Problem in multivariate gaussian\n distance : %f - det %f\n", distance.coeff (0,0), det);
        cout << "Cov \n" << cov << endl << "Cov inverse \n" << cov_inverse << endl;
        return 0.f;
    }

    return res;
}

void  GMPHD::predictBirth()
{
    m_spawnTargets.m_gaussians.clear ();
    m_birthTargets.m_gaussians.clear ();

    // -----------------------------------------
    // Compute spontaneous births
    m_birthTargets.m_gaussians = m_birthModel.m_gaussians;
    m_nPredictedTargets += m_birthTargets.m_gaussians.size ();

    // -----------------------------------------
    // Compute spawned targets
    GaussianModel new_spawn;

    for (unsigned int k=0; k< m_currTargets.m_gaussians.size (); ++k)
    {
        for (unsigned int i=0; i< m_spawnModels.size (); ++i)
        {
            // Define a gaussian model from the existing target
            // and spawning properties
            new_spawn.m_weight = m_currTargets.m_gaussians[k].m_weight
                               * m_spawnModels[i].m_weight;

            new_spawn.m_mean = m_spawnModels[i].m_offset
                             + m_spawnModels[i].m_trans * m_currTargets.m_gaussians[k].m_mean;

            new_spawn.m_cov = m_spawnModels[i].m_cov
                            + m_spawnModels[i].m_trans *
                            m_currTargets.m_gaussians[k].m_cov *
                            m_spawnModels[i].m_trans.transpose();

            // Add this new gaussian to the list of expected targets
            m_spawnTargets.m_gaussians.push_back (new_spawn);

            // Update the number of expected targets
            m_nPredictedTargets ++;
        }
    }
}

void  GMPHD::predictTargets () {
    GaussianModel new_target;

    m_expTargets.m_gaussians.resize(m_currTargets.m_gaussians.size ());

    for (unsigned int i=0; i<m_currTargets.m_gaussians.size (); ++i)
    {
        // Compute the new shape of the target
        new_target.m_weight = m_pSurvival * m_currTargets.m_gaussians[i].m_weight;

        new_target.m_mean = m_tgtDynTrans * m_currTargets.m_gaussians[i].m_mean;

        new_target.m_cov = m_tgtDynCov +
                         m_tgtDynTrans * m_currTargets.m_gaussians[i].m_cov * m_tgtDynTrans.transpose();

        // Push back to the expected targets
        m_expTargets.m_gaussians[i] = new_target;

        ++m_nPredictedTargets;
    }
}

void GMPHD::print()
{
    printf("Current gaussian mixture : \n");

    for (unsigned int i=0; i< m_currTargets.m_gaussians.size(); ++i)
    {
        printf("Gaussian %d - pos %.1f  %.1f %.1f - cov %.1f  %.1f %.1f - weight %.3f\n",
               i,
               m_currTargets.m_gaussians[i].m_mean(0,0),
               m_currTargets.m_gaussians[i].m_mean(1,0),
               m_currTargets.m_gaussians[i].m_mean(2,0),
               m_currTargets.m_gaussians[i].m_cov(0,0),
               m_currTargets.m_gaussians[i].m_cov(1,1),
               m_currTargets.m_gaussians[i].m_cov(2,2),
               m_currTargets.m_gaussians[i].m_weight) ;
    }
    printf("\n");
}

void  GMPHD::propagate ()
{
    m_nPredictedTargets = 0;

    // Predict new targets (spawns):
    predictBirth();

    // Predict propagation of expected targets :
    predictTargets();

    // Build the update components
    buildUpdate ();

    if(m_bVerbose)
    {
        printf("\nGMPHD_propagate :--- Expected targets : %d ---\n", m_nPredictedTargets);
        m_expTargets.print();
    }

    // Update GMPHD
    update();

    if (m_bVerbose)
    {
        printf("\nGMPHD_propagate :--- \n");
        m_currTargets.print ();
    }

    // Prune gaussians (remove weakest, merge close enough gaussians)
    pruneGaussians ();

    if (m_bVerbose)
    {
        printf("\nGMPHD_propagate :--- Pruned targets : ---\n");
        m_currTargets.print();
    }

    // Clean vectors :
    m_expMeasure.clear ();
    m_expDisp.clear ();
    m_uncertainty.clear ();
    m_covariance.clear ();
}

void  GMPHD::pruneGaussians()
{
    m_currTargets.prune( m_pruneTruncThld, m_pruneMergeThld, m_nMaxPrune );
}

void GMPHD::reset()
{
    m_currTargets.m_gaussians.clear ();
    m_extractedTargets.m_gaussians.clear ();
}


void  GMPHD::setBirthModel(vector<GaussianModel> &birth_model)
{
    m_birthModel.m_gaussians.clear ();
    m_birthModel.m_gaussians = birth_model;
}

void  GMPHD::setDynamicsModel(float sampling, float process_noise)
{

    m_samplingPeriod  = sampling;
    m_processNoise    = process_noise;

    // Fill in propagation matrix :
    m_tgtDynTrans = MatrixXf::Identity(m_dimState, m_dimState);

    for (int i = 0; i<m_dimMeasures; ++i)
    {
        m_tgtDynTrans(i,m_dimMeasures+i) = m_samplingPeriod;
    }

    // Fill in covariance matrix
    // Extra covariance added by the dynamics. Could be 0.
    m_tgtDynCov = process_noise * process_noise *
                          MatrixXf::Identity(m_dimState, m_dimState);

    //  // FIXME: hardcoded crap !
    //  _tgt_dyn_covariance(0,0) = powf (sampling, 4.f)/4.f;
    //  _tgt_dyn_covariance(1,1) = powf (sampling, 4.f)/4.f;
    //  _tgt_dyn_covariance(2,2) = powf (sampling, 4.f)/4.f;

    //  _tgt_dyn_covariance(3,3) = powf (sampling, 2.f);
    //  _tgt_dyn_covariance(4,4) = powf (sampling, 2.f);
    //  _tgt_dyn_covariance(5,5) = powf (sampling, 2.f);

    //  _tgt_dyn_covariance(0,3) = powf (sampling, 3.f)/2.f;
    //  _tgt_dyn_covariance(1,4) = powf (sampling, 3.f)/2.f;
    //  _tgt_dyn_covariance(2,5) = powf (sampling, 3.f)/2.f;

    //  _tgt_dyn_covariance(3,0) = powf (sampling, 3.f)/2.f;
    //  _tgt_dyn_covariance(4,1) = powf (sampling, 3.f)/2.f;
    //  _tgt_dyn_covariance(5,1) = powf (sampling, 3.f)/2.f;

    //  _tgt_dyn_covariance = _tgt_dyn_covariance *
    //                       (_process_noise * _process_noise);
    // \FIXME
}

void GMPHD::setDynamicsModel(MatrixXf &tgt_dyn_transitions, MatrixXf &tgt_dyn_covariance)
{
    m_tgtDynTrans = tgt_dyn_transitions;
    m_tgtDynCov = tgt_dyn_covariance;
}

void  GMPHD::setNewMeasurements(vector<float> const & position,
                                vector<float> const & speed)
{
    // Clear the gaussian mixture
    m_measTargets.m_gaussians.clear();

    GaussianModel new_obs;

    int iTarget = 0;

    while(iTarget < position.size()/m_dimMeasures)
    {
        new_obs.clear();

        for (unsigned int i=0; i< m_dimMeasures; ++i) {
            // Create new gaussian model according to measurement
            new_obs.m_mean(i) = position[iTarget*m_dimMeasures + i];
            new_obs.m_mean(i+m_dimMeasures) = speed[iTarget*m_dimMeasures + i];

            new_obs.m_cov = m_obsCov;

            new_obs.m_weight = 1.f;

            m_measTargets.m_gaussians.push_back(new_obs);
        }

        iTarget++;
    }
}

void  GMPHD::setNewReferential(const Matrix4f *transform)
{
    // Change referential for every gaussian in the gaussian mixture
    m_currTargets.changeReferential(transform);
}



void  GMPHD::setPruningParameters (float  prune_trunc_thld,
                                   float  prune_merge_thld,
                                   int    prune_max_nb)
{

    m_pruneTruncThld = prune_trunc_thld;
    m_pruneMergeThld = prune_merge_thld;
    m_nMaxPrune     = prune_max_nb;
}


void  GMPHD::setObservationModel( float prob_detection_overall,
                                  float measurement_noise_pose,
                                  float measurement_noise_speed,
                                  float measurement_background )
{
    m_pDetection      = prob_detection_overall;
    m_measNoisePose   = measurement_noise_pose;
    m_measNoiseSpeed  = measurement_noise_speed;
    m_measNoiseBackground   = measurement_background; // False detection probability

    // Set model matrices
    m_obsMat  = MatrixXf::Identity(m_dimState, m_dimState);
    m_obsMatT = m_obsMat.transpose();
    m_obsCov  = MatrixXf::Identity(m_dimState,m_dimState);

    // FIXME: deal with the _motion_model parameter !
    m_obsCov.block(0,0,m_dimMeasures, m_dimMeasures) *= m_measNoisePose * m_measNoisePose;
    m_obsCov.block(m_dimMeasures,m_dimMeasures,m_dimMeasures, m_dimMeasures) *= m_measNoiseSpeed * m_measNoiseSpeed;
}

void  GMPHD::setSpawnModel(vector <SpawningModel> & spawnModels)
{
    // Stupid implementation, maybe to be improved..
    for (auto const & model : spawnModels)
    {
        m_spawnModels.push_back( model);
    }
}

void  GMPHD::setSurvivalProbability(float _prob_survival)
{
    m_pSurvival = _prob_survival;
}

void  GMPHD::update()
{
    int n_meas, n_targt, index;
    m_currTargets.m_gaussians.clear();

    // We'll consider every possible association : vector size is (expected targets)*(measured targets)
    m_currTargets.m_gaussians.resize((m_measTargets.m_gaussians.size () + 1) *
                                        m_expTargets.m_gaussians.size ());

    // First set of gaussians : mere propagation of existing ones
    // \warning : don't propagate the "birth" targets...
    // we set their weight to 0

    m_nPredictedTargets =  m_expTargets.m_gaussians.size ();
    int i_birth_current = 0;
    for (int i=0; i<m_nPredictedTargets; ++i) {
        if (i != m_iBirthTargets[i_birth_current]) {
            m_currTargets.m_gaussians[i].m_weight = (1.f - m_pDetection) *
                                                     m_expTargets.m_gaussians[i].m_weight;
        } else {
            i_birth_current = min(i_birth_current+1, (int) m_iBirthTargets.size ());
            m_currTargets.m_gaussians[i].m_weight = 0.f;
        }

        m_currTargets.m_gaussians[i].m_mean = m_expTargets.m_gaussians[i].m_mean;
        m_currTargets.m_gaussians[i].m_cov  = m_expTargets.m_gaussians[i].m_cov;
    }


    // Second set of gaussians : match observations and previsions
    if (m_measTargets.m_gaussians.size () == 0)
    {
        return;
    }
    else
    {
        for (n_meas=1; n_meas <= m_measTargets.m_gaussians.size (); ++n_meas)
        {
            for (n_targt = 0; n_targt < m_nPredictedTargets; ++n_targt)
            {
                index = n_meas * m_nPredictedTargets + n_targt;

                // Compute matching factor between predictions and measures.
                // \warning : we only take positions into account there
                m_currTargets.m_gaussians[index].m_weight =  m_pDetection *
                                                              m_expTargets.m_gaussians[n_targt].m_weight *
                                                              gaussDensity_3D(m_measTargets.m_gaussians[n_meas -1].m_mean.block(0,0,3,1),
                        m_expMeasure[n_targt].block(0,0,3,1),
                        m_expDisp[n_targt].block(0,0,3,3));

                m_currTargets.m_gaussians[index].m_mean =  m_expTargets.m_gaussians[n_targt].m_mean +
                                                            m_uncertainty[n_targt] *
                                                            (m_measTargets.m_gaussians[n_meas -1].m_mean - m_expMeasure[n_targt]);

                m_currTargets.m_gaussians[index].m_cov = m_covariance[n_targt];
            }

            // Normalize weights in the same predicted set,
            // taking clutter into account
            m_currTargets.normalize (m_measNoiseBackground, n_meas * m_nPredictedTargets,
                                        (n_meas + 1) * m_nPredictedTargets, 1);
        }
    }
}

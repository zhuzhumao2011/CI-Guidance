#pragma once
#include <Eigen/dense>
#include <qvector.h>
#include <qstring.h>
#include <qdatetime.h>

class patient_data
{
public:
    patient_data(QString filename); // reads filename.ini and parses patient data
	~patient_data(void);
    bool parse(void);
    quint32 id();
    QDate date();
    Eigen::Matrix3Xd fiducials() const;
    Eigen::Matrix3Xd trajectory() const;
    Eigen::Vector3d target();
    Eigen::Vector3d axis();

private:
    QString iniFileName;
    quint32 m_id;
    QDate m_date;
    Eigen::Matrix3Xd m_fiducials;
    Eigen::Matrix3Xd m_trajectory;
    Eigen::Vector3d m_target;
    Eigen::Vector3d m_axis;
};


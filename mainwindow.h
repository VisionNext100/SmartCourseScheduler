#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardItemModel>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHash>
#include <QMenu>
#include <QAction>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class Course {
public:
    QString id;
    QString name;
    int credit;
    QString semester; // "Autumn" or "Spring"
    QString required; // "Compulsory" or "Elective"

    struct Offering {
        QString id;
        QString teacher;
        QList<int> times; // 7 days, 13 bits each
        int weeks;        // 18-bit weeks mask
    };

    QList<Offering> offerings;
    QList<QString> prerequisites;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_loadCoursesButton_clicked();
    void on_generateScheduleButton_clicked();
    void on_exportScheduleButton_clicked();
    void on_addCourseButton_clicked();
    void on_editCourseButton_clicked();
    void on_removeCourseButton_clicked();
    void on_setConstraintsButton_clicked();
    void on_manageConsecutiveCoursesButton_clicked();
    void showContextMenu(const QPoint &pos);
    void on_searchTextChanged(const QString &text);
    void on_actionAbout_triggered();

private:
    Ui::MainWindow *ui;
    QList<Course> m_courses;
    QStandardItemModel *m_courseModel;
    QList<QStandardItemModel*> m_scheduleModels;
    QList<int> m_semesterCredits;
    QHash<QString, int> m_courseSemesterMap; // 课程ID -> 学期
    QHash<QString, QString> m_courseOfferingMap; // 课程ID -> 教学班ID

    // 约束条件
    QList<int> m_maxCreditsPerSemester;
    QList<QList<QList<int>>> m_forbiddenSlots; // 每个学期的禁排时间段 [学期][天][时间段]

    // 连续课程管理
    QHash<QString, QString> m_consecutiveCourseMap; // 存储连续课程关系：前导课程ID -> 后续课程ID

    bool loadCourses(const QString &filePath);
    bool saveSchedule(const QString &filePath);
    void displayCourses();
    void displaySchedule();
    bool hasTimeConflict(const Course &a, const Course &b);
    bool hasTimeConflictWithOffering(const Course &a, const Course &b, const Course::Offering &offeringA);
    bool hasForbiddenTimeConflict(const Course &course, int semester);
    bool hasForbiddenTimeConflictWithOffering(const Course &course, int semester, const Course::Offering &offering);
    bool isPrerequisiteSatisfied(const QString &courseId, int currentSemester);
    bool canCourseBeOfferedInSemester(const Course &course, int semester);
    void generateSchedule(int minCredits);
    void scheduleCourse(const Course &course, int minCredits);
    void fillSemesterToMaximum(int semester, const QList<Course> &availableCourses, int minCredits);
    bool validateScheduleTimeConflicts();
    bool validatePrerequisites();
    bool validateConsecutiveCourses();
    int calculateAvailableTimeSlots(int semester);
    void addConsecutiveCoursePair(const QString &preCourseId, const QString &postCourseId);
    void removeConsecutiveCoursePair(const QString &preCourseId);
    QString getConsecutiveCourse(const QString &courseId) const;
    QList<QString> getPrecedingCourses(const QString &courseId) const;
    void scheduleConsecutiveCourses();
    void scheduleConsecutiveCoursePairs(const QList<Course> &availableCourses);
    void updateCreditSummary();
    const Course* findCourseById(const QString &id) const;
    QString formatWeeksString(int weeksMask) const;
};

#endif // MAINWINDOW_H

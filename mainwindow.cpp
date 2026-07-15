#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QTableView>
#include <QMenu>
#include <QAction>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QSet>
#include <QQueue>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 连接搜索框信号
    connect(ui->searchLineEdit, &QLineEdit::textChanged, this, &MainWindow::on_searchTextChanged);

    // 初始化课程表格模型
    m_courseModel = new QStandardItemModel(this);
    m_courseModel->setHorizontalHeaderLabels({"ID", "名称", "学分", "学期", "类型", "先修课程"});
    ui->courseTableView->setModel(m_courseModel);
    ui->courseTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->courseTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->courseTableView, &QTableView::customContextMenuRequested, this, &MainWindow::showContextMenu);

    // 初始化8个学期的课表模型
    for (int i = 0; i < 8; ++i) {
        QStandardItemModel *model = new QStandardItemModel(13, 7, this);
        model->setHorizontalHeaderLabels({"周一", "周二", "周三", "周四", "周五", "周六", "周日"});
        m_scheduleModels.append(model);
        m_semesterCredits.append(0);
        m_maxCreditsPerSemester.append(100);
        // 初始化禁排时间段：8学期，每学期7天，每天13节课
        QList<QList<int>> semesterForbidden;
        for (int day = 0; day < 7; ++day) {
            semesterForbidden.append(QList<int>());
        }
        m_forbiddenSlots.append(semesterForbidden);

        QTableView *tableView = new QTableView();
        tableView->setModel(model);
        tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableView->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tableView, &QTableView::customContextMenuRequested, this, &MainWindow::showContextMenu);

        // 移除空白：直接设置tableView为tab的内容，不使用layout
        QWidget *tabWidget = ui->scheduleTabWidget->widget(i);
        QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(tabWidget->layout());
        if (layout) {
            // 清除现有布局中的所有项目
            QLayoutItem *item;
            while ((item = layout->takeAt(0)) != nullptr) {
                delete item;
            }
            // 添加tableView
            layout->addWidget(tableView);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
        }
    }

    setStyleSheet(
        "QTabWidget::pane { border: 1px solid #ccc; }"
        "QTabBar::tab { padding: 8px; }"
        "QTableView { selection-background-color: #e6f3ff; }"
        );

    ui->scheduleTabWidget->setCurrentIndex(0);
    updateCreditSummary();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    QTableView *tableView = qobject_cast<QTableView*>(sender());
    if (!tableView) return;

    QMenu menu(this);
    QAction *adjustRow = menu.addAction("调整行高");
    QAction *adjustCol = menu.addAction("调整列宽");

    QAction *selected = menu.exec(tableView->viewport()->mapToGlobal(pos));
    if (selected == adjustRow) {
        tableView->resizeRowsToContents();
    } else if (selected == adjustCol) {
        tableView->resizeColumnsToContents();
    }
}

void MainWindow::on_searchTextChanged(const QString &text)
{
    // 实现真正的搜索功能
    if (text.isEmpty()) {
        // 如果搜索框为空，显示所有课程
        for (int row = 0; row < m_courseModel->rowCount(); ++row) {
            ui->courseTableView->setRowHidden(row, false);
        }
        return;
    }

    // 搜索逻辑：在课程ID、名称、学期、类型中搜索
    for (int row = 0; row < m_courseModel->rowCount(); ++row) {
        bool match = false;

        // 搜索课程ID（第0列）
        QStandardItem *idItem = m_courseModel->item(row, 0);
        if (idItem && idItem->text().contains(text, Qt::CaseInsensitive)) {
            match = true;
        }

        // 搜索课程名称（第1列）
        if (!match) {
            QStandardItem *nameItem = m_courseModel->item(row, 1);
            if (nameItem && nameItem->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
            }
        }

        // 搜索学期（第3列）
        if (!match) {
            QStandardItem *semesterItem = m_courseModel->item(row, 3);
            if (semesterItem && semesterItem->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
            }
        }

        // 搜索课程类型（第4列）
        if (!match) {
            QStandardItem *typeItem = m_courseModel->item(row, 4);
            if (typeItem && typeItem->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
            }
        }

        // 搜索先修课程（第5列）
        if (!match) {
            QStandardItem *preReqItem = m_courseModel->item(row, 5);
            if (preReqItem && preReqItem->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
            }
        }

        // 显示或隐藏行
        ui->courseTableView->setRowHidden(row, !match);
    }
}

void MainWindow::on_loadCoursesButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择课程文件", "", "JSON文件 (*.json)");
    if (filePath.isEmpty()) return;

    if (loadCourses(filePath)) {
        displayCourses();
        QMessageBox::information(this, "成功", "课程导入成功！");
    } else {
        QMessageBox::warning(this, "错误", "课程导入失败！");
    }
}

bool MainWindow::loadCourses(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) return false;

    m_courses.clear();
    QJsonArray coursesArray = doc.array();

    for (const QJsonValue &courseValue : coursesArray) {
        QJsonObject courseObj = courseValue.toObject();
        Course course;

        course.id = courseObj["id"].toString();
        course.name = courseObj["name"].toString();
        course.credit = courseObj["credit"].toInt();
        course.semester = courseObj["semester"].toString();
        course.required = courseObj["required"].toString();

        QJsonArray offeringsArray = courseObj["offerings"].toArray();
        for (const QJsonValue &offeringValue : offeringsArray) {
            QJsonObject offeringObj = offeringValue.toObject();
            Course::Offering offering;

            offering.id = offeringObj["id"].toString();
            offering.teacher = offeringObj["teacher"].toString();

            QJsonArray timesArray = offeringObj["times"].toArray();
            for (const QJsonValue &timeValue : timesArray) {
                offering.times.append(timeValue.toInt());
            }

            offering.weeks = offeringObj["weeks"].toInt();
            course.offerings.append(offering);
        }

        QJsonArray preReqsArray = courseObj["prerequisites"].toArray();
        for (const QJsonValue &preReqValue : preReqsArray) {
            course.prerequisites.append(preReqValue.toString());
        }

        m_courses.append(course);
    }

    return true;
}

void MainWindow::displayCourses()
{
    m_courseModel->removeRows(0, m_courseModel->rowCount());

    for (const Course &course : m_courses) {
        QList<QStandardItem*> rowItems;
        rowItems.append(new QStandardItem(course.id));
        rowItems.append(new QStandardItem(course.name));
        rowItems.append(new QStandardItem(QString::number(course.credit)));
        rowItems.append(new QStandardItem(course.semester));
        rowItems.append(new QStandardItem(course.required));

        QString preReqs;
        for (const QString &preReq : course.prerequisites) {
            preReqs += preReq + ", ";
        }
        if (!preReqs.isEmpty()) preReqs.chop(2);
        rowItems.append(new QStandardItem(preReqs));

        m_courseModel->appendRow(rowItems);
    }
}

void MainWindow::on_generateScheduleButton_clicked()
{
    bool ok;
    int minCredits = QInputDialog::getInt(this, "输入学分要求",
                                          "请输入最低总学分要求:", 100, 0, 500, 10, &ok);
    if (!ok) return;

    generateSchedule(minCredits);
    displaySchedule();
}

void MainWindow::generateSchedule(int minCredits)
{
    // 重置课表和学分统计
    for (int i = 0; i < 8; ++i) {
        m_scheduleModels[i]->removeRows(0, m_scheduleModels[i]->rowCount());
        m_semesterCredits[i] = 0;
    }
    m_courseSemesterMap.clear();
    m_courseOfferingMap.clear();

    // 创建课程副本用于排序和选择
    QList<Course> availableCourses = m_courses;

    // 计算可用课程的总学分
    int totalAvailableCredits = 0;
    for (const Course &course : availableCourses) {
        totalAvailableCredits += course.credit;
    }

    // 计算每学期学分上限之和
    int totalMaxCredits = 0;
    for (int maxCredits : m_maxCreditsPerSemester) {
        totalMaxCredits += maxCredits;
    }

    // 检查每学期学分上限之和是否小于最低总学分要求
    if (totalMaxCredits < minCredits) {
        QMessageBox::warning(this, "警告", QString("每学期学分上限之和(%1)小于最低总学分要求(%2)，无法生成课表")
                                                 .arg(totalMaxCredits).arg(minCredits));
        return;
    }

    if (totalAvailableCredits < minCredits) {
        QMessageBox::warning(this, "警告", QString("可用课程总学分(%1)不足，无法生成%2学分的排课方案")
                                                 .arg(totalAvailableCredits).arg(minCredits));
        return;
    }

    // 第一阶段：安排连续课程对
    scheduleConsecutiveCoursePairs(availableCourses);

    // 第二阶段：按学期逐个最大化填充，严格不超出学分上限
    for (int semester = 0; semester < 8; ++semester) {
        fillSemesterToMaximum(semester, availableCourses, minCredits);
    }

    // 重新计算总学分
    int totalCredits = 0;
    for (int credits : m_semesterCredits) {
        totalCredits += credits;
    }

    // 验证时间冲突和先修关系
    bool hasTimeConflicts = !validateScheduleTimeConflicts();
    bool hasPrerequisiteViolations = !validatePrerequisites();
    bool hasConsecutiveViolations = !validateConsecutiveCourses();

    if (totalCredits < minCredits) {
        QMessageBox::warning(this, "警告", QString("无法生成满足%1学分的排课方案，当前总学分为%2\n可用课程总学分: %3")
                                                 .arg(minCredits).arg(totalCredits).arg(totalAvailableCredits));
    } else {
        QString message = QString("排课方案生成成功！总学分: %1").arg(totalCredits);
        bool hasWarnings = false;

        if (hasTimeConflicts) {
            message += "\n⚠️ 警告：检测到时间冲突，请检查课表！";
            hasWarnings = true;
        }

        if (hasPrerequisiteViolations) {
            message += "\n⚠️ 警告：检测到先修关系违反，请检查课表！";
            hasWarnings = true;
        }

        if (hasConsecutiveViolations) {
            message += "\n⚠️ 警告：检测到连续课程约束违反，请检查课表！";
            hasWarnings = true;
        }

        if (hasWarnings) {
            QMessageBox::warning(this, "成功但有约束违反", message);
        } else {
            QMessageBox::information(this, "成功", message);
        }
    }
}

void MainWindow::scheduleCourse(const Course &course, int minCredits)
{
    // 如果课程已经安排，跳过
    if (m_courseSemesterMap.contains(course.id)) {
        return;
    }

    // 优先在前几个学期安排课程，提高总学分
    // 计算当前已安排的课程数量，优先填充前几个学期
    QList<int> semesterLoads;
    for (int i = 0; i < 8; ++i) {
        int load = 0;
        for (auto it = m_courseSemesterMap.begin(); it != m_courseSemesterMap.end(); ++it) {
            if (it.value() == i) {
                load++;
            }
        }
        semesterLoads.append(load);
    }

    // 按学期负载排序，优先选择负载较轻的学期
    QList<int> semesterOrder;
    for (int i = 0; i < 8; ++i) {
        semesterOrder.append(i);
    }

    std::sort(semesterOrder.begin(), semesterOrder.end(), [&semesterLoads](int a, int b) {
        return semesterLoads[a] < semesterLoads[b];
    });

    // 尝试将课程安排到负载最轻的合适学期
    for (int semester : semesterOrder) {
        // 严格检查学分上限，不允许超出
        if (m_semesterCredits[semester] + course.credit > m_maxCreditsPerSemester[semester]) {
            continue;
        }

        // 检查先修关系
        if (!isPrerequisiteSatisfied(course.id, semester)) {
            continue;
        }

        // 检查课程是否可以在该学期开课（春季/秋季学期限制）
        if (!canCourseBeOfferedInSemester(course, semester)) {
            continue;
        }

        // 尝试所有教学班，找到没有时间冲突的
        bool scheduled = false;
        for (const Course::Offering &offering : course.offerings) {
            bool conflict = false;

            // 检查与已安排课程的时间冲突
            for (auto it = m_courseSemesterMap.begin(); it != m_courseSemesterMap.end(); ++it) {
                if (it.value() == semester) {
                    const Course *scheduledCourse = findCourseById(it.key());
                    if (scheduledCourse && hasTimeConflictWithOffering(course, *scheduledCourse, offering)) {
                        conflict = true;
                        break;
                    }
                }
            }

            // 检查禁排时间段冲突
            if (!conflict && hasForbiddenTimeConflictWithOffering(course, semester, offering)) {
                conflict = true;
            }

            if (!conflict) {
                // 安排课程
                m_courseSemesterMap[course.id] = semester;
                m_semesterCredits[semester] += course.credit;

                // 记录选择的教学班
                m_courseOfferingMap[course.id] = offering.id;

                scheduled = true;
                break;
            }
        }

        if (scheduled) {
            break;
        }
    }
}



bool MainWindow::hasTimeConflict(const Course &a, const Course &b)
{
    for (const Course::Offering &offeringA : a.offerings) {
        for (const Course::Offering &offeringB : b.offerings) {
            for (int day = 0; day < 7; ++day) {
                if ((offeringA.times[day] & offeringB.times[day]) != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool MainWindow::hasTimeConflictWithOffering(const Course &a, const Course &b, const Course::Offering &offeringA)
{
    // 获取课程b已选择的教学班
    QString selectedOfferingId = m_courseOfferingMap.value(b.id, "");
    const Course::Offering* selectedOfferingB = nullptr;

    if (!selectedOfferingId.isEmpty()) {
        for (const Course::Offering &offering : b.offerings) {
            if (offering.id == selectedOfferingId) {
                selectedOfferingB = &offering;
                break;
            }
        }
    }

    // 如果没有找到选择的教学班，检查所有教学班（更严格的时间冲突检查）
    if (!selectedOfferingB) {
        for (const Course::Offering &offeringB : b.offerings) {
            for (int day = 0; day < 7; ++day) {
                if ((offeringA.times[day] & offeringB.times[day]) != 0) {
                    return true;
                }
            }
        }
    } else {
        // 只检查已选择的教学班
        for (int day = 0; day < 7; ++day) {
            if ((offeringA.times[day] & selectedOfferingB->times[day]) != 0) {
                return true;
            }
        }
    }

    return false;
}

bool MainWindow::hasForbiddenTimeConflict(const Course &course, int semester)
{
    if (semester < 0 || semester >= m_forbiddenSlots.size()) {
        return false;
    }

    const QList<QList<int>> &semesterForbidden = m_forbiddenSlots[semester];

    for (const Course::Offering &offering : course.offerings) {
        for (int day = 0; day < 7; ++day) {
            if (day >= semesterForbidden.size()) continue;

            const QList<int> &dayForbidden = semesterForbidden[day];
            if (dayForbidden.isEmpty()) continue;

            int timeBits = offering.times[day];
            for (int lesson = 0; lesson < 13; ++lesson) {
                if (timeBits & (1 << lesson)) {
                    // 检查这节课是否在禁排时间段内
                    if (dayForbidden.contains(lesson + 1)) { // lesson从0开始，但用户输入从1开始
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool MainWindow::hasForbiddenTimeConflictWithOffering(const Course &course, int semester, const Course::Offering &offering)
{
    if (semester < 0 || semester >= m_forbiddenSlots.size()) {
        return false;
    }

    const QList<QList<int>> &semesterForbidden = m_forbiddenSlots[semester];

    for (int day = 0; day < 7; ++day) {
        if (day >= semesterForbidden.size()) continue;

        const QList<int> &dayForbidden = semesterForbidden[day];
        if (dayForbidden.isEmpty()) continue;

        int timeBits = offering.times[day];
        for (int lesson = 0; lesson < 13; ++lesson) {
            if (timeBits & (1 << lesson)) {
                // 检查这节课是否在禁排时间段内
                if (dayForbidden.contains(lesson + 1)) { // lesson从0开始，但用户输入从1开始
                    return true;
                }
            }
        }
    }
    return false;
}

bool MainWindow::isPrerequisiteSatisfied(const QString &courseId, int currentSemester)
{
    const Course *course = findCourseById(courseId);
    if (!course) return false;

    // 严格检查先修关系：所有先修课程必须在当前学期之前完成
    for (const QString &preReq : course->prerequisites) {
        // 先修课程必须已经安排
        if (!m_courseSemesterMap.contains(preReq)) {
            return false;
        }
        // 先修课程必须在当前学期之前完成
        if (m_courseSemesterMap[preReq] >= currentSemester) {
            return false;
        }
    }
    return true;
}

bool MainWindow::canCourseBeOfferedInSemester(const Course &course, int semester)
{
    // 学期从0开始，但用户习惯从1开始
    int semesterNumber = semester + 1;

    if (course.semester == "Autumn") {
        // 秋季课程只能在1,3,5,7学期开课
        return (semesterNumber == 1 || semesterNumber == 3 || semesterNumber == 5 || semesterNumber == 7);
    } else if (course.semester == "Spring") {
        // 春季课程只能在2,4,6,8学期开课
        return (semesterNumber == 2 || semesterNumber == 4 || semesterNumber == 6 || semesterNumber == 8);
    }

    return false;
}

bool MainWindow::validateScheduleTimeConflicts()
{
    // 验证整个课表是否有时间冲突
    for (int semester = 0; semester < 8; ++semester) {
        QList<QString> semesterCourses;

        // 收集该学期的所有课程
        for (auto it = m_courseSemesterMap.begin(); it != m_courseSemesterMap.end(); ++it) {
            if (it.value() == semester) {
                semesterCourses.append(it.key());
            }
        }

        // 检查该学期内课程之间的时间冲突
        for (int i = 0; i < semesterCourses.size(); ++i) {
            for (int j = i + 1; j < semesterCourses.size(); ++j) {
                const Course *courseA = findCourseById(semesterCourses[i]);
                const Course *courseB = findCourseById(semesterCourses[j]);

                if (!courseA || !courseB) continue;

                QString offeringIdA = m_courseOfferingMap.value(semesterCourses[i], "");
                QString offeringIdB = m_courseOfferingMap.value(semesterCourses[j], "");

                const Course::Offering *offeringA = nullptr;
                const Course::Offering *offeringB = nullptr;

                // 找到选择的教学班
                for (const Course::Offering &offering : courseA->offerings) {
                    if (offering.id == offeringIdA) {
                        offeringA = &offering;
                        break;
                    }
                }

                for (const Course::Offering &offering : courseB->offerings) {
                    if (offering.id == offeringIdB) {
                        offeringB = &offering;
                        break;
                    }
                }

                if (!offeringA || !offeringB) continue;

                // 严格检查时间冲突
                for (int day = 0; day < 7; ++day) {
                    if ((offeringA->times[day] & offeringB->times[day]) != 0) {
                        qDebug() << "时间冲突检测到：" << courseA->name << "和" << courseB->name
                                 << "在第" << (semester + 1) << "学期第" << (day + 1) << "天";
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool MainWindow::validatePrerequisites()
{
    // 验证所有课程的先修关系是否满足
    for (auto it = m_courseSemesterMap.begin(); it != m_courseSemesterMap.end(); ++it) {
        const Course *course = findCourseById(it.key());
        if (!course) continue;

        int semester = it.value();

        // 检查该课程的所有先修课程
        for (const QString &preReq : course->prerequisites) {
            // 先修课程必须已经安排
            if (!m_courseSemesterMap.contains(preReq)) {
                qDebug() << "先修关系违反：" << course->name << "的先修课程" << preReq << "未安排";
                return false;
            }

            // 先修课程必须在当前课程之前完成
            if (m_courseSemesterMap[preReq] >= semester) {
                qDebug() << "先修关系违反：" << course->name << "的先修课程" << preReq
                         << "在第" << (m_courseSemesterMap[preReq] + 1) << "学期，"
                         << "但" << course->name << "在第" << (semester + 1) << "学期";
                return false;
            }
        }
    }

    return true;
}

bool MainWindow::validateConsecutiveCourses()
{
    // 验证连续课程约束是否满足
    for (auto it = m_consecutiveCourseMap.begin(); it != m_consecutiveCourseMap.end(); ++it) {
        QString preCourseId = it.key();
        QString postCourseId = it.value();

        // 检查两门课程是否都已安排
        if (!m_courseSemesterMap.contains(preCourseId) || !m_courseSemesterMap.contains(postCourseId)) {
            qDebug() << "连续课程约束违反：" << preCourseId << "或" << postCourseId << "未安排";
            return false;
        }

        int preSemester = m_courseSemesterMap[preCourseId];
        int postSemester = m_courseSemesterMap[postCourseId];

        // 检查是否在相邻学期
        if (postSemester != preSemester + 1) {
            const Course *preCourse = findCourseById(preCourseId);
            const Course *postCourse = findCourseById(postCourseId);
            qDebug() << "连续课程约束违反：" << (preCourse ? preCourse->name : preCourseId)
                     << "在第" << (preSemester + 1) << "学期，"
                     << (postCourse ? postCourse->name : postCourseId)
                     << "在第" << (postSemester + 1) << "学期，不是相邻学期";
            return false;
        }
    }

    return true;
}

int MainWindow::calculateAvailableTimeSlots(int semester)
{
    // 计算指定学期的可用时间槽数量
    // 总时间槽：7天 × 13节课 = 91个时间槽
    int totalSlots = 7 * 13;
    int usedSlots = 0;

    // 收集该学期的所有课程
    QList<QString> semesterCourses;
    for (auto it = m_courseSemesterMap.begin(); it != m_courseSemesterMap.end(); ++it) {
        if (it.value() == semester) {
            semesterCourses.append(it.key());
        }
    }

    // 计算已使用的时间槽
    for (const QString &courseId : semesterCourses) {
        const Course *course = findCourseById(courseId);
        if (!course) continue;

        QString offeringId = m_courseOfferingMap.value(courseId, "");
        const Course::Offering *offering = nullptr;

        // 找到选择的教学班
        for (const Course::Offering &off : course->offerings) {
            if (off.id == offeringId) {
                offering = &off;
                break;
            }
        }

        if (!offering) continue;

        // 计算该课程使用的时间槽
        for (int day = 0; day < 7; ++day) {
            int timeBits = offering->times[day];
            for (int lesson = 0; lesson < 13; ++lesson) {
                if (timeBits & (1 << lesson)) {
                    usedSlots++;
                }
            }
        }
    }

    // 减去禁排时间段
    if (semester < m_forbiddenSlots.size()) {
        const QList<QList<int>> &semesterForbidden = m_forbiddenSlots[semester];
        for (int day = 0; day < 7 && day < semesterForbidden.size(); ++day) {
            usedSlots += semesterForbidden[day].size();
        }
    }

    return totalSlots - usedSlots;
}

void MainWindow::fillSemesterToMaximum(int semester, const QList<Course> &availableCourses, int minCredits)
{
    int availableSlots = calculateAvailableTimeSlots(semester);
    while (availableSlots > 0) {
        bool addedCourse = false;
        QList<Course> remainingCourses;
        for (const Course &course : availableCourses) {
            if (!m_courseSemesterMap.contains(course.id) && !course.offerings.isEmpty()) {
                if (canCourseBeOfferedInSemester(course, semester) && isPrerequisiteSatisfied(course.id, semester)) {
                    // 检查连续课程约束：如果该课程有前导课程，确保前导课程已经安排
                    bool consecutiveConstraintSatisfied = true;
                    QList<QString> precedingCourses = getPrecedingCourses(course.id);
                    for (const QString &preCourseId : precedingCourses) {
                        if (!m_courseSemesterMap.contains(preCourseId)) {
                            consecutiveConstraintSatisfied = false;
                            break;
                        }
                    }

                    // 检查连续课程约束：如果该课程是某门课程的前导课程，且后续课程已经安排，确保在正确学期
                    QString nextCourseId = getConsecutiveCourse(course.id);
                    if (!nextCourseId.isEmpty() && m_courseSemesterMap.contains(nextCourseId)) {
                        int nextSemester = m_courseSemesterMap[nextCourseId];
                        if (semester >= nextSemester) {
                            consecutiveConstraintSatisfied = false;
                        }
                    }

                    if (consecutiveConstraintSatisfied) {
                        remainingCourses.append(course);
                    }
                }
            }
        }
        std::sort(remainingCourses.begin(), remainingCourses.end(), [](const Course &a, const Course &b) {
            if (a.credit != b.credit) return a.credit > b.credit;
            if (a.required != b.required) return a.required == "Compulsory" && b.required == "Elective";
            return a.prerequisites.size() < b.prerequisites.size();
        });
        for (const Course &course : remainingCourses) {
            int maxAllowed = m_maxCreditsPerSemester[semester];
            if (m_semesterCredits[semester] + course.credit > maxAllowed) continue;
            for (const Course::Offering &offering : course.offerings) {
                bool conflict = false;
                for (auto it = m_courseSemesterMap.begin(); it != m_courseSemesterMap.end(); ++it) {
                    if (it.value() == semester) {
                        const Course *scheduledCourse = findCourseById(it.key());
                        if (scheduledCourse && hasTimeConflictWithOffering(course, *scheduledCourse, offering)) {
                            conflict = true;
                            break;
                        }
                    }
                }
                if (!conflict && hasForbiddenTimeConflictWithOffering(course, semester, offering)) conflict = true;
                if (!conflict) {
                    m_courseSemesterMap[course.id] = semester;
                    m_semesterCredits[semester] += course.credit;
                    m_courseOfferingMap[course.id] = offering.id;
                    addedCourse = true;
                    break;
                }
            }
            if (addedCourse) break;
        }
        if (!addedCourse) break;
        availableSlots = calculateAvailableTimeSlots(semester);
    }
}

void MainWindow::addConsecutiveCoursePair(const QString &preCourseId, const QString &postCourseId)
{
    // 检查课程是否存在
    if (!findCourseById(preCourseId) || !findCourseById(postCourseId)) {
        return;
    }

    // 检查是否形成循环依赖
    if (preCourseId == postCourseId) {
        return;
    }

    // 检查是否已经存在相同的连续课程关系
    if (m_consecutiveCourseMap.contains(preCourseId) && m_consecutiveCourseMap[preCourseId] == postCourseId) {
        return;
    }

    // 添加连续课程关系
    m_consecutiveCourseMap[preCourseId] = postCourseId;
}

void MainWindow::removeConsecutiveCoursePair(const QString &preCourseId)
{
    m_consecutiveCourseMap.remove(preCourseId);
}

QString MainWindow::getConsecutiveCourse(const QString &courseId) const
{
    return m_consecutiveCourseMap.value(courseId, "");
}

QList<QString> MainWindow::getPrecedingCourses(const QString &courseId) const
{
    QList<QString> precedingCourses;
    for (auto it = m_consecutiveCourseMap.begin(); it != m_consecutiveCourseMap.end(); ++it) {
        if (it.value() == courseId) {
            precedingCourses.append(it.key());
        }
    }
    return precedingCourses;
}

void MainWindow::scheduleConsecutiveCourses()
{
    // 根据连续课程关系调整排课顺序
    // 确保前导课程在后续课程之前安排

    // 构建课程依赖图
    QHash<QString, QList<QString>> dependencies; // 课程ID -> 依赖该课程的课程列表
    QHash<QString, int> inDegree; // 课程ID -> 入度

    // 初始化
    for (const Course &course : m_courses) {
        dependencies[course.id] = QList<QString>();
        inDegree[course.id] = 0;
    }

    // 添加连续课程依赖关系
    for (auto it = m_consecutiveCourseMap.begin(); it != m_consecutiveCourseMap.end(); ++it) {
        QString preCourse = it.key();
        QString postCourse = it.value();

        dependencies[preCourse].append(postCourse);
        inDegree[postCourse]++;
    }

    // 拓扑排序，确保前导课程优先安排
    QList<QString> sortedCourses;
    QQueue<QString> queue;

    // 将入度为0的课程加入队列
    for (auto it = inDegree.begin(); it != inDegree.end(); ++it) {
        if (it.value() == 0) {
            queue.enqueue(it.key());
        }
    }

    // 拓扑排序
    while (!queue.isEmpty()) {
        QString current = queue.dequeue();
        sortedCourses.append(current);

        for (const QString &dependent : dependencies[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                queue.enqueue(dependent);
            }
        }
    }

    // 检查是否有循环依赖
    if (sortedCourses.size() != m_courses.size()) {
        qDebug() << "警告：检测到循环依赖，可能影响连续课程安排";
    }

    // 重新排序课程列表，确保前导课程在前
    QList<Course> reorderedCourses;
    for (const QString &courseId : sortedCourses) {
        for (const Course &course : m_courses) {
            if (course.id == courseId) {
                reorderedCourses.append(course);
                break;
            }
        }
    }

    // 将重新排序的课程列表用于后续排课
    // 注意：这里我们只是重新排序，实际的排课逻辑在fillSemesterToMaximum中
    // 由于m_courses是const引用，我们通过调整排课顺序来实现连续课程约束
}

void MainWindow::scheduleConsecutiveCoursePairs(const QList<Course> &availableCourses)
{
    // 专门处理连续课程对的安排
    // 确保前导课程和后续课程安排在相邻学期

    qDebug() << "开始安排连续课程对，共有" << m_consecutiveCourseMap.size() << "对连续课程";

    for (auto it = m_consecutiveCourseMap.begin(); it != m_consecutiveCourseMap.end(); ++it) {
        QString preCourseId = it.key();
        QString postCourseId = it.value();

        // 如果已经安排，跳过
        if (m_courseSemesterMap.contains(preCourseId) || m_courseSemesterMap.contains(postCourseId)) {
            continue;
        }

        // 找到前导课程和后续课程
        const Course *preCourse = findCourseById(preCourseId);
        const Course *postCourse = findCourseById(postCourseId);

        if (!preCourse || !postCourse) {
            continue;
        }

        // 尝试安排前导课程
        bool preScheduled = false;
        for (int semester = 0; semester < 7; ++semester) { // 最多到第7学期，给后续课程留空间
            // 检查前导课程是否可以在该学期开课
            if (!canCourseBeOfferedInSemester(*preCourse, semester)) {
                continue;
            }

            // 检查学分上限
            if (m_semesterCredits[semester] + preCourse->credit > m_maxCreditsPerSemester[semester]) {
                continue;
            }

            // 检查先修关系
            if (!isPrerequisiteSatisfied(preCourseId, semester)) {
                continue;
            }

            // 尝试安排前导课程
            bool conflict = false;
            for (const Course::Offering &offering : preCourse->offerings) {
                conflict = false;

                // 检查与已安排课程的时间冲突
                for (auto scheduledIt = m_courseSemesterMap.begin(); scheduledIt != m_courseSemesterMap.end(); ++scheduledIt) {
                    if (scheduledIt.value() == semester) {
                        const Course *scheduledCourse = findCourseById(scheduledIt.key());
                        if (scheduledCourse && hasTimeConflictWithOffering(*preCourse, *scheduledCourse, offering)) {
                            conflict = true;
                            break;
                        }
                    }
                }

                // 检查禁排时间段冲突
                if (!conflict && hasForbiddenTimeConflictWithOffering(*preCourse, semester, offering)) {
                    conflict = true;
                }

                if (!conflict) {
                    // 安排前导课程
                    m_courseSemesterMap[preCourseId] = semester;
                    m_semesterCredits[semester] += preCourse->credit;
                    m_courseOfferingMap[preCourseId] = offering.id;
                    preScheduled = true;
                    qDebug() << "成功安排前导课程:" << preCourseId << "在第" << (semester + 1) << "学期";
                    break;
                }
            }

            if (preScheduled) {
                break;
            }
        }

        // 如果前导课程安排成功，尝试安排后续课程
        if (preScheduled) {
            int preSemester = m_courseSemesterMap[preCourseId];
            int postSemester = preSemester + 1;

            // 检查后续课程是否可以在下一学期开课
            if (postSemester < 8 && canCourseBeOfferedInSemester(*postCourse, postSemester)) {
                // 检查学分上限
                if (m_semesterCredits[postSemester] + postCourse->credit <= m_maxCreditsPerSemester[postSemester]) {
                    // 检查先修关系
                    if (isPrerequisiteSatisfied(postCourseId, postSemester)) {
                        // 尝试安排后续课程
                        for (const Course::Offering &offering : postCourse->offerings) {
                            bool conflict = false;

                            // 检查与已安排课程的时间冲突
                            for (auto scheduledIt = m_courseSemesterMap.begin(); scheduledIt != m_courseSemesterMap.end(); ++scheduledIt) {
                                if (scheduledIt.value() == postSemester) {
                                    const Course *scheduledCourse = findCourseById(scheduledIt.key());
                                    if (scheduledCourse && hasTimeConflictWithOffering(*postCourse, *scheduledCourse, offering)) {
                                        conflict = true;
                                        break;
                                    }
                                }
                            }

                            // 检查禁排时间段冲突
                            if (!conflict && hasForbiddenTimeConflictWithOffering(*postCourse, postSemester, offering)) {
                                conflict = true;
                            }

                            if (!conflict) {
                                // 安排后续课程
                                m_courseSemesterMap[postCourseId] = postSemester;
                                m_semesterCredits[postSemester] += postCourse->credit;
                                m_courseOfferingMap[postCourseId] = offering.id;
                                qDebug() << "成功安排后续课程:" << postCourseId << "在第" << (postSemester + 1) << "学期";
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

const Course* MainWindow::findCourseById(const QString &id) const
{
    for (const Course &course : m_courses) {
        if (course.id == id) return &course;
    }
    return nullptr;
}

QString MainWindow::formatWeeksString(int weeksMask) const
{
    if (weeksMask == 0) return "";

    QList<int> weeks;
    for (int i = 0; i < 30; ++i) { // 支持1-30周
        if (weeksMask & (1 << i)) {
            weeks.append(i + 1);
        }
    }

    if (weeks.isEmpty()) return "";

    // 检查是否为连续周次
    bool isConsecutive = true;
    for (int i = 1; i < weeks.size(); ++i) {
        if (weeks[i] != weeks[i-1] + 1) {
            isConsecutive = false;
            break;
        }
    }

    if (isConsecutive) {
        // 连续周次，如 1-18
        return QString("%1-%2").arg(weeks.first()).arg(weeks.last());
    } else {
        // 检查是否为单数周或双数周
        bool isOddWeeks = true;
        bool isEvenWeeks = true;

        for (int week : weeks) {
            if (week % 2 == 0) { // 双数周
                isOddWeeks = false;
            } else { // 单数周
                isEvenWeeks = false;
            }
        }

        if (isOddWeeks) {
            // 单数周，如 1-18（单）
            return QString("%1-%2（单）").arg(weeks.first()).arg(weeks.last());
        } else if (isEvenWeeks) {
            // 双数周，如 1-18（双）
            return QString("%1-%2（双）").arg(weeks.first()).arg(weeks.last());
        } else {
            // 其他情况，显示具体周次
            QStringList weekStrings;
            for (int week : weeks) {
                weekStrings.append(QString::number(week));
            }
            return weekStrings.join(",");
        }
    }
}

void MainWindow::displaySchedule()
{
    // 清空并初始化所有课表
    for (int i = 0; i < 8; ++i) {
        QStandardItemModel* model = m_scheduleModels[i];
        model->clear();
        model->setRowCount(13);
        model->setColumnCount(7);
        model->setHorizontalHeaderLabels({"周一", "周二", "周三", "周四", "周五", "周六", "周日"});

        for (int row = 0; row < 13; ++row) {
            QStandardItem* timeItem = new QStandardItem(QString("第%1节").arg(row + 1));
            model->setVerticalHeaderItem(row, timeItem);

            for (int col = 0; col < 7; ++col) {
                QStandardItem* item = new QStandardItem();
                item->setTextAlignment(Qt::AlignCenter);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setData(Qt::TextWordWrap, Qt::TextAlignmentRole);
                model->setItem(row, col, item);
            }
        }
    }

    // 填充课表内容
    for (auto it = m_courseSemesterMap.begin(); it != m_courseSemesterMap.end(); ++it) {
        const Course* course = findCourseById(it.key());
        if (!course) continue;

        int semester = it.value();
        QString selectedOfferingId = m_courseOfferingMap.value(it.key(), "");

        // 找到选择的教学班
        const Course::Offering* selectedOffering = nullptr;
        if (!selectedOfferingId.isEmpty()) {
            for (const Course::Offering &offering : course->offerings) {
                if (offering.id == selectedOfferingId) {
                    selectedOffering = &offering;
                    break;
                }
            }
        }

        // 如果没有找到选择的教学班，使用第一个
        if (!selectedOffering && !course->offerings.isEmpty()) {
            selectedOffering = &course->offerings.first();
        }

        if (selectedOffering) {
            for (int day = 0; day < 7; ++day) {
                int timeBits = selectedOffering->times[day];
                for (int lesson = 0; lesson < 13; ++lesson) {
                    if (timeBits & (1 << lesson)) {
                        QStandardItem* cell = m_scheduleModels[semester]->item(lesson, day);

                        // 格式化周次信息
                        QString weeksStr = formatWeeksString(selectedOffering->weeks);

                        // 在课表单元格中显示课程名称、教师和周次
                        QString cellText = QString("%1\n%2").arg(course->name).arg(selectedOffering->teacher);
                        if (!weeksStr.isEmpty()) {
                            cellText += QString("\n%1").arg(weeksStr);
                        }
                        cell->setText(cellText);

                        // 在工具提示中显示详细信息
                        QString tooltip = QString("%1\n教师: %2\n学分: %3\n教学班: %4").arg(course->name).arg(selectedOffering->teacher).arg(course->credit).arg(selectedOffering->id);
                        if (!weeksStr.isEmpty()) {
                            tooltip += QString("\n周次: %1").arg(weeksStr);
                        }
                        cell->setToolTip(tooltip);
                    }
                }
            }
        }
    }

    // 更新UI显示
    for (int i = 0; i < 8; ++i) {
        QWidget *tabWidget = ui->scheduleTabWidget->widget(i);
        QTableView* tableView = qobject_cast<QTableView*>(tabWidget->layout()->itemAt(0)->widget());

        if (tableView) {
            tableView->setWordWrap(true);
            tableView->setTextElideMode(Qt::ElideNone);
            tableView->verticalHeader()->setDefaultSectionSize(100); // 增加行高以容纳周次信息
            tableView->horizontalHeader()->setDefaultSectionSize(150);
            tableView->resizeRowsToContents();
            tableView->resizeColumnsToContents();
            tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);

            tableView->setStyleSheet(
                "QTableView {"
                "  gridline-color: #ddd;"
                "  font-size: 10pt;"
                "}"
                "QTableView::item {"
                "  padding: 5px;"
                "  border: 1px solid #eee;"
                "}"
                );
        }

        ui->scheduleTabWidget->setTabText(i, QString("第%1学期 (%2学分)").arg(i + 1).arg(m_semesterCredits[i]));
    }

    updateCreditSummary();
}

void MainWindow::updateCreditSummary()
{
    int total = 0;
    for (int credits : m_semesterCredits) {
        total += credits;
    }
    ui->creditLabel->setText(QString("总学分: %1").arg(total));
}

void MainWindow::on_exportScheduleButton_clicked()
{
    QString filePath = QFileDialog::getSaveFileName(this, "导出排课方案", "", "JSON文件 (*.json)");
    if (filePath.isEmpty()) return;

    if (saveSchedule(filePath)) {
        QMessageBox::information(this, "成功", "排课方案导出成功！");
    } else {
        QMessageBox::warning(this, "错误", "排课方案导出失败！");
    }
}

bool MainWindow::saveSchedule(const QString &filePath)
{
    QJsonArray scheduleArray;

    for (const Course &course : m_courses) {
        QJsonObject courseObj;
        courseObj["course_id"] = course.id;
        courseObj["semester"] = m_courseSemesterMap.value(course.id, -1);
        courseObj["class_id"] = m_courseOfferingMap.value(course.id, "");

        scheduleArray.append(courseObj);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    file.write(QJsonDocument(scheduleArray).toJson());
    return true;
}

void MainWindow::on_addCourseButton_clicked()
{
    QDialog dialog(this);
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit idEdit(&dialog), nameEdit(&dialog), preReqEdit(&dialog);
    QSpinBox creditSpin(&dialog);
    QComboBox semesterCombo(&dialog), typeCombo(&dialog);
    QLineEdit weeksEdit(&dialog), timesEdit(&dialog);

    semesterCombo.addItems({"Autumn", "Spring"});
    typeCombo.addItems({"Compulsory", "Elective"});
    creditSpin.setRange(1, 10);
    preReqEdit.setPlaceholderText("请用逗号分隔多个先修课程id");
    weeksEdit.setPlaceholderText("如: 1-16 或 1,2,3,5,7");
    timesEdit.setPlaceholderText("如: 1:1-2,3:3-4 (周一1-2节,周三3-4节)");

    form->addRow("课程ID:", &idEdit);
    form->addRow("课程名称:", &nameEdit);
    form->addRow("学分:", &creditSpin);
    form->addRow("开课学期:", &semesterCombo);
    form->addRow("课程类型:", &typeCombo);
    form->addRow("先修课程:", &preReqEdit);
    form->addRow("上课周次:", &weeksEdit);
    form->addRow("上课时间段:", &timesEdit);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form->addRow(&buttonBox);

    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        Course newCourse;
        newCourse.id = idEdit.text();
        newCourse.name = nameEdit.text();
        newCourse.credit = creditSpin.value();
        newCourse.semester = semesterCombo.currentText();
        newCourse.required = typeCombo.currentText();

        // 解析先修课程
        QString preReqText = preReqEdit.text().trimmed();
        if (!preReqText.isEmpty()) {
            QStringList preReqs = preReqText.split(',');
            for (const QString &preReq : preReqs) {
                QString trimmedPreReq = preReq.trimmed();
                if (!trimmedPreReq.isEmpty()) {
                    newCourse.prerequisites.append(trimmedPreReq);
                }
            }
        }

        Course::Offering offering;
        offering.id = "01";
        offering.teacher = "教师";
        offering.times = QList<int>(7, 0);
        offering.weeks = 0;

        // 解析上课周次
        QString weeksText = weeksEdit.text().trimmed();
        if (!weeksText.isEmpty()) {
            QSet<int> weekSet;
            QStringList weekParts = weeksText.split(',');
            for (const QString &part : weekParts) {
                QString trimmed = part.trimmed();
                if (trimmed.contains('-')) {
                    QStringList range = trimmed.split('-');
                    if (range.size() == 2) {
                        bool ok1, ok2;
                        int start = range[0].toInt(&ok1);
                        int end = range[1].toInt(&ok2);
                        if (ok1 && ok2 && start >= 1 && end <= 30 && start <= end) {
                            for (int w = start; w <= end; ++w) weekSet.insert(w);
                        }
                    }
                } else {
                    bool ok;
                    int w = trimmed.toInt(&ok);
                    if (ok && w >= 1 && w <= 30) weekSet.insert(w);
                }
            }
            // weeks字段用bitmask表示，假设支持1-30周
            int weeksMask = 0;
            for (int w : weekSet) {
                weeksMask |= (1 << (w-1));
            }
            offering.weeks = weeksMask;
        } else {
            // 默认1-16周
            offering.weeks = 0xFFFF;
        }

        // 解析上课时间段
        // times: 7天，每天13节课，每天一个int的bitmask
        QString timesText = timesEdit.text().trimmed();
        if (!timesText.isEmpty()) {
            QStringList timeParts = timesText.split(',');
            for (const QString &part : timeParts) {
                QString trimmed = part.trimmed();
                // 格式如 1:1-2
                int colonIdx = trimmed.indexOf(':');
                if (colonIdx > 0) {
                    bool okDay;
                    int day = trimmed.left(colonIdx).toInt(&okDay); // 1=周一
                    if (okDay && day >= 1 && day <= 7) {
                        QString section = trimmed.mid(colonIdx+1);
                        if (section.contains('-')) {
                            QStringList secRange = section.split('-');
                            if (secRange.size() == 2) {
                                bool ok1, ok2;
                                int start = secRange[0].toInt(&ok1);
                                int end = secRange[1].toInt(&ok2);
                                if (ok1 && ok2 && start >= 1 && end <= 13 && start <= end) {
                                    for (int s = start; s <= end; ++s) {
                                        offering.times[day-1] |= (1 << (s-1));
                                    }
                                }
                            }
                        } else {
                            bool ok;
                            int s = section.toInt(&ok);
                            if (ok && s >= 1 && s <= 13) {
                                offering.times[day-1] |= (1 << (s-1));
                            }
                        }
                    }
                }
            }
        }

        newCourse.offerings.append(offering);
        m_courses.append(newCourse);
        displayCourses();
    }
}

void MainWindow::on_editCourseButton_clicked()
{
    QModelIndex index = ui->courseTableView->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, "警告", "请先选择要编辑的课程");
        return;
    }

    QString courseId = m_courseModel->item(index.row(), 0)->text();
    Course *course = nullptr;

    for (Course &c : m_courses) {
        if (c.id == courseId) {
            course = &c;
            break;
        }
    }

    if (!course) return;

    QDialog dialog(this);
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit nameEdit(course->name, &dialog);
    QSpinBox creditSpin(&dialog);
    creditSpin.setValue(course->credit);
    QComboBox semesterCombo(&dialog);
    semesterCombo.addItems({"Autumn", "Spring"});
    semesterCombo.setCurrentText(course->semester);
    QComboBox typeCombo(&dialog);
    typeCombo.addItems({"Compulsory", "Elective"});
    typeCombo.setCurrentText(course->required);

    QLineEdit preReqEdit(&dialog);
    preReqEdit.setPlaceholderText("请用逗号分隔多个先修课程id");

    // 显示当前先修课程
    QString currentPreReqs;
    for (int i = 0; i < course->prerequisites.size(); ++i) {
        if (i > 0) currentPreReqs += ",";
        currentPreReqs += course->prerequisites[i];
    }
    preReqEdit.setText(currentPreReqs);

    form->addRow("课程名称:", &nameEdit);
    form->addRow("学分:", &creditSpin);
    form->addRow("开课学期:", &semesterCombo);
    form->addRow("课程类型:", &typeCombo);
    form->addRow("先修课程:", &preReqEdit);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form->addRow(&buttonBox);

    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        course->name = nameEdit.text();
        course->credit = creditSpin.value();
        course->semester = semesterCombo.currentText();
        course->required = typeCombo.currentText();

        // 更新先修课程
        course->prerequisites.clear();
        QString preReqText = preReqEdit.text().trimmed();
        if (!preReqText.isEmpty()) {
            QStringList preReqs = preReqText.split(',');
            for (const QString &preReq : preReqs) {
                QString trimmedPreReq = preReq.trimmed();
                if (!trimmedPreReq.isEmpty()) {
                    course->prerequisites.append(trimmedPreReq);
                }
            }
        }

        displayCourses();
    }
}

void MainWindow::on_removeCourseButton_clicked()
{
    QModelIndex index = ui->courseTableView->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, "警告", "请先选择要删除的课程");
        return;
    }

    QString courseId = m_courseModel->item(index.row(), 0)->text();

    if (QMessageBox::question(this, "确认", "确定要删除此课程吗？") == QMessageBox::Yes) {
        for (int i = 0; i < m_courses.size(); ++i) {
            if (m_courses[i].id == courseId) {
                m_courses.removeAt(i);
                break;
            }
        }
        displayCourses();
    }
}

void MainWindow::on_manageConsecutiveCoursesButton_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("连续课程管理");
    dialog.resize(600, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    // 添加连续课程对
    QGroupBox *addGroup = new QGroupBox("添加连续课程对", &dialog);
    QFormLayout *addForm = new QFormLayout(addGroup);

    QLineEdit *preCourseEdit = new QLineEdit(&dialog);
    QLineEdit *postCourseEdit = new QLineEdit(&dialog);
    preCourseEdit->setPlaceholderText("前导课程ID");
    postCourseEdit->setPlaceholderText("后续课程ID");

    addForm->addRow("前导课程ID:", preCourseEdit);
    addForm->addRow("后续课程ID:", postCourseEdit);

    QPushButton *addButton = new QPushButton("添加", &dialog);
    addForm->addRow(addButton);

    mainLayout->addWidget(addGroup);

    // 显示当前连续课程关系
    QGroupBox *listGroup = new QGroupBox("当前连续课程关系", &dialog);
    QVBoxLayout *listLayout = new QVBoxLayout(listGroup);

    QListWidget *consecutiveList = new QListWidget(&dialog);
    listLayout->addWidget(consecutiveList);

    // 刷新列表函数
    auto refreshList = [&]() {
        consecutiveList->clear();
        for (auto it = m_consecutiveCourseMap.begin(); it != m_consecutiveCourseMap.end(); ++it) {
            const Course *preCourse = findCourseById(it.key());
            const Course *postCourse = findCourseById(it.value());
            QString itemText = QString("%1 (%2) → %3 (%4)")
                                   .arg(it.key())
                                   .arg(preCourse ? preCourse->name : "未知课程")
                                   .arg(it.value())
                                   .arg(postCourse ? postCourse->name : "未知课程");
            QListWidgetItem *item = new QListWidgetItem(itemText);
            item->setData(Qt::UserRole, it.key()); // 存储前导课程ID
            consecutiveList->addItem(item);
        }
    };

    refreshList();

    // 删除按钮
    QPushButton *removeButton = new QPushButton("删除选中", &dialog);
    listLayout->addWidget(removeButton);

    mainLayout->addWidget(listGroup);

    // 按钮
    QDialogButtonBox buttonBox(QDialogButtonBox::Close, Qt::Horizontal, &dialog);
    mainLayout->addWidget(&buttonBox);

    // 连接信号
    connect(addButton, &QPushButton::clicked, [&]() {
        QString preId = preCourseEdit->text().trimmed();
        QString postId = postCourseEdit->text().trimmed();

        if (preId.isEmpty() || postId.isEmpty()) {
            QMessageBox::warning(&dialog, "错误", "请输入课程ID");
            return;
        }

        if (!findCourseById(preId)) {
            QMessageBox::warning(&dialog, "错误", QString("前导课程 '%1' 不存在").arg(preId));
            return;
        }

        if (!findCourseById(postId)) {
            QMessageBox::warning(&dialog, "错误", QString("后续课程 '%1' 不存在").arg(postId));
            return;
        }

        if (preId == postId) {
            QMessageBox::warning(&dialog, "错误", "前导课程和后续课程不能相同");
            return;
        }

        // 检查是否形成循环依赖
        QSet<QString> visited;
        QString current = postId;
        while (!current.isEmpty()) {
            if (visited.contains(current)) {
                QMessageBox::warning(&dialog, "错误", "添加此关系会形成循环依赖");
                return;
            }
            visited.insert(current);
            current = getConsecutiveCourse(current);
        }

        addConsecutiveCoursePair(preId, postId);
        refreshList();

        preCourseEdit->clear();
        postCourseEdit->clear();

        QMessageBox::information(&dialog, "成功", "连续课程关系添加成功");
    });

    connect(removeButton, &QPushButton::clicked, [&]() {
        QListWidgetItem *currentItem = consecutiveList->currentItem();
        if (!currentItem) {
            QMessageBox::warning(&dialog, "错误", "请先选择要删除的关系");
            return;
        }

        QString preId = currentItem->data(Qt::UserRole).toString();
        removeConsecutiveCoursePair(preId);
        refreshList();

        QMessageBox::information(&dialog, "成功", "连续课程关系删除成功");
    });

    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

void MainWindow::on_setConstraintsButton_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("设置约束条件");
    dialog.resize(700, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    // 学分上限设置
    QGroupBox *creditGroup = new QGroupBox("学分上限设置", &dialog);
    QFormLayout *creditForm = new QFormLayout(creditGroup);
    QList<QSpinBox*> creditSpins;

    for (int i = 0; i < 8; ++i) {
        QSpinBox *spin = new QSpinBox(&dialog);
        spin->setRange(0, 100);
        spin->setValue(m_maxCreditsPerSemester[i]);
        creditForm->addRow(QString("第%1学期学分上限:").arg(i+1), spin);
        creditSpins.append(spin);
    }
    mainLayout->addWidget(creditGroup);

    // 禁排时间设置
    QGroupBox *forbiddenGroup = new QGroupBox("禁排时间设置", &dialog);
    QVBoxLayout *forbiddenLayout = new QVBoxLayout(forbiddenGroup);

    QLabel *instructionLabel = new QLabel("格式说明：输入格式为 '星期几:时间段'，例如：\n"
                                          "• 1:1-3 表示周一1-3节不排课\n"
                                          "• 1:1-2,3:6-8 表示周一1-2节和周三6-8节不排课\n"
                                          "• 2:5,7 表示周二第5、7节不排课\n"
                                          "• 留空表示该学期不设置禁排时间", &dialog);
    instructionLabel->setWordWrap(true);
    forbiddenLayout->addWidget(instructionLabel);

    QList<QLineEdit*> forbiddenEdits; // 每个学期一个输入框

    for (int semester = 0; semester < 8; ++semester) {
        QGroupBox *semesterGroup = new QGroupBox(QString("第%1学期").arg(semester + 1), &dialog);
        QFormLayout *semesterForm = new QFormLayout(semesterGroup);

        QLineEdit *edit = new QLineEdit(&dialog);
        edit->setPlaceholderText("例如: 1:1-3,3:6-8 或留空");

        // 显示当前禁排时间段设置
        QString currentForbidden;
        if (semester < m_forbiddenSlots.size()) {
            const QList<QList<int>> &semesterForbidden = m_forbiddenSlots[semester];
            for (int day = 0; day < 7 && day < semesterForbidden.size(); ++day) {
                const QList<int> &dayForbidden = semesterForbidden[day];
                if (!dayForbidden.isEmpty()) {
                    if (!currentForbidden.isEmpty()) currentForbidden += ",";
                    currentForbidden += QString("%1:").arg(day + 1);

                    for (int j = 0; j < dayForbidden.size(); ++j) {
                        if (j > 0) currentForbidden += ",";
                        currentForbidden += QString::number(dayForbidden[j]);
                    }
                }
            }
        }
        edit->setText(currentForbidden);

        semesterForm->addRow("禁排时间:", edit);
        forbiddenEdits.append(edit);
        forbiddenLayout->addWidget(semesterGroup);
    }

    mainLayout->addWidget(forbiddenGroup);

    // 按钮
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    mainLayout->addWidget(&buttonBox);

    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QString errorMessage;
        bool hasError = false;

        // 保存学分上限
        for (int i = 0; i < 8; ++i) {
            m_maxCreditsPerSemester[i] = creditSpins[i]->value();
        }

        // 保存禁排时间
        for (int semester = 0; semester < 8; ++semester) {
            // 确保学期数据结构存在
            while (m_forbiddenSlots.size() <= semester) {
                QList<QList<int>> semesterForbidden;
                for (int day = 0; day < 7; ++day) {
                    semesterForbidden.append(QList<int>());
                }
                m_forbiddenSlots.append(semesterForbidden);
            }

            // 清空当前学期的禁排时间
            for (int day = 0; day < 7; ++day) {
                m_forbiddenSlots[semester][day].clear();
            }

            // 解析每个学期的禁排时间
            QString text = forbiddenEdits[semester]->text().trimmed();
            if (!text.isEmpty()) {
                // 解析格式：1:1-3,3:6-8
                QStringList dayParts = text.split(',');
                for (const QString &dayPart : dayParts) {
                    QString trimmedDayPart = dayPart.trimmed();
                    if (trimmedDayPart.isEmpty()) continue;

                    // 检查是否包含冒号（表示指定星期几）
                    int colonIdx = trimmedDayPart.indexOf(':');
                    if (colonIdx > 0) {
                        // 格式：1:1-3 或 1:5,7
                        bool okDay;
                        int day = trimmedDayPart.left(colonIdx).toInt(&okDay);
                        if (!okDay || day < 1 || day > 7) {
                            errorMessage = QString("第%1学期禁排时间星期几格式错误：'%2'（应为1-7）")
                                               .arg(semester + 1).arg(trimmedDayPart.left(colonIdx));
                            hasError = true;
                            break;
                        }

                        QString timeSection = trimmedDayPart.mid(colonIdx + 1);
                        QStringList timeParts = timeSection.split(',');
                        for (const QString &timePart : timeParts) {
                            QString trimmedTimePart = timePart.trimmed();
                            if (trimmedTimePart.isEmpty()) continue;

                            if (trimmedTimePart.contains('-')) {
                                // 处理范围，如 1-3
                                QStringList range = trimmedTimePart.split('-');
                                if (range.size() != 2) {
                                    errorMessage = QString("第%1学期禁排时间格式错误：'%2' 不是有效的范围格式（应为 start-end）")
                                                       .arg(semester + 1).arg(trimmedTimePart);
                                    hasError = true;
                                    break;
                                }

                                bool ok1, ok2;
                                int start = range[0].trimmed().toInt(&ok1);
                                int end = range[1].trimmed().toInt(&ok2);

                                if (!ok1 || !ok2) {
                                    errorMessage = QString("第%1学期禁排时间包含非数字：'%2'")
                                                       .arg(semester + 1).arg(trimmedTimePart);
                                    hasError = true;
                                    break;
                                }

                                if (start < 1 || start > 13) {
                                    errorMessage = QString("第%1学期禁排时间范围起始值超出范围：%2（应为1-13）")
                                                       .arg(semester + 1).arg(start);
                                    hasError = true;
                                    break;
                                }

                                if (end < 1 || end > 13) {
                                    errorMessage = QString("第%1学期禁排时间范围结束值超出范围：%2（应为1-13）")
                                                       .arg(semester + 1).arg(end);
                                    hasError = true;
                                    break;
                                }

                                if (start > end) {
                                    errorMessage = QString("第%1学期禁排时间范围错误：起始值%2大于结束值%3")
                                                       .arg(semester + 1).arg(start).arg(end);
                                    hasError = true;
                                    break;
                                }

                                for (int j = start; j <= end; ++j) {
                                    if (!m_forbiddenSlots[semester][day - 1].contains(j)) {
                                        m_forbiddenSlots[semester][day - 1].append(j);
                                    }
                                }
                            } else {
                                // 处理单个数字
                                bool ok;
                                int slot = trimmedTimePart.toInt(&ok);

                                if (!ok) {
                                    errorMessage = QString("第%1学期禁排时间包含非数字：'%2'")
                                                       .arg(semester + 1).arg(trimmedTimePart);
                                    hasError = true;
                                    break;
                                }

                                if (slot < 1 || slot > 13) {
                                    errorMessage = QString("第%1学期禁排时间超出范围：%2（应为1-13）")
                                                       .arg(semester + 1).arg(slot);
                                    hasError = true;
                                    break;
                                }

                                if (!m_forbiddenSlots[semester][day - 1].contains(slot)) {
                                    m_forbiddenSlots[semester][day - 1].append(slot);
                                }
                            }
                        }

                        if (hasError) break;
                    } else {
                        // 没有冒号，按原来的格式处理（兼容旧格式）
                        errorMessage = QString("第%1学期禁排时间格式错误：'%2' 缺少星期几标识（应为 day:time 格式）")
                                           .arg(semester + 1).arg(trimmedDayPart);
                        hasError = true;
                        break;
                    }
                }
            }

            if (hasError) break;
        }

        if (hasError) {
            QMessageBox::warning(this, "输入错误", errorMessage);
        } else {
            QMessageBox::information(this, "成功", "约束条件设置成功！");
        }
    }
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::information(this, "关于", "使用指南详见《课程项目报告书》");
}
